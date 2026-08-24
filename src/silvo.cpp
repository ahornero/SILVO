/*
 * SILVO – Simplified Light–Vegetation Overlay Model. Version 1.1
 * Description:
 *   Core ray-tracing module of the SILVO model. This component implements
 *   backward (eye) ray tracing to simulate geometric illumination within
 *   heterogeneous vegetation canopies represented as collections of spheres
 *   and vertically aligned spheroids.
 *   The model computes structural metrics such as gap fraction, transmission,
 *   reflection and cumulative vegetation path length, and produces a simple
 *   rendered scene for visual interpretation and a BIP file for analysis.
 *
 * Author:
 *   Alberto Hornero
 *
 * License:
 *   This program is free software: you can redistribute it and/or modify it
 *   under the terms of the GNU Affero General Public License version 3
 *   (AGPLv3) as published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the AGPLv3
 *   license for more details.
 *
 *   A copy of the license should have been included with this program. If not,
 *   see <https://www.gnu.org/licenses/>.
 *
 * Citation:
 *   If you use SILVO in academic work, please cite the corresponding article:
 *
 *   Hornero, A., Prikaziuk, E., Hernandez-Clemente, R., van der Tol, C. (2026).
 *   SILVO, a lightweight 3D illumination model to characterise the spatial structure of heterogeneous vegetation canopies.
 *   International Journal of Applied Earth Observation and Geoinformation, 147, 105178. https://doi.org/10.1016/j.jag.2026.105178
 */

#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <vector>
#include <iostream>
#include <cassert>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <omp.h>
#include <getopt.h>

// Windows support, not fully tested
#if defined _WIN32 || defined _WIN64
#define M_PI 3.141592653589793
#define INFINITY 1e8
#endif

#define SOIL_COLOUR Vec3f(0.85, 0.70, 0.40)
#define SKY_COLOUR Vec3f(0.25, 0.72, 0.77)
#define SOIL_SPHERE_RADIUS 6371000
#define PROFILE_STEPS 50

unsigned debug = 0;
const unsigned width = 1280, height = 960;

typedef double Real; // 64 bit floating point

enum class Surface {
    SOIL,
    VEGETATION,
    SKY,
    LIGHT
};

struct Settings {
    Real sza = 0; 	// solar zenith angle
    Real saa = 0; 	// solar azimuth angle
    Real cza = 0; 	// camera zenith angle
    Real caa = 0; 	// camera azimuth angle
    Real cds = 600; // camera distance
    Real fov = 30;  // camera field of view
};

struct Stats {
    Real sunlit_soil;
    Real shaded_soil;
    Real sunlit_vegetation;
    Real shaded_vegetation;
    Real normalised_density;
};

class Vec3f {
public:
    Real x, y, z;
    Vec3f() : x(0), y(0), z(0) {}
    Vec3f(Real xx) : x(xx), y(xx), z(xx) {}
    Vec3f(Real xx, Real yy, Real zz) : x(xx), y(yy), z(zz) {}

    Vec3f& normalize() {
        Real len2 = length2();
        if (len2 > 0) {
            Real invLen = 1 / sqrt(len2);
            x *= invLen;
            y *= invLen;
            z *= invLen;
        }
        return *this;
    }

    Real dot(const Vec3f& v) const { return x * v.x + y * v.y + z * v.z; }

    Vec3f cross(const Vec3f& v) const {
        return Vec3f(
            y * v.z - z * v.y,
            z * v.x - x * v.z,
            x * v.y - y * v.x
        );
    }

    Vec3f operator * (Real f) const { return Vec3f(x * f, y * f, z * f); }
    
    Vec3f operator + (const Vec3f& v) const { return Vec3f(x + v.x, y + v.y, z + v.z); }
    Vec3f operator - (const Vec3f& v) const { return Vec3f(x - v.x, y - v.y, z - v.z); }
    Vec3f operator * (const Vec3f& v) const { return Vec3f(x * v.x, y * v.y, z * v.z); }


    Real length2() const { return x * x + y * y + z * z; }
    Real length() const { return sqrt(length2()); }

    friend std::ostream& operator << (std::ostream &os, const Vec3f &v) {
        os << "(" << v.x << ", " << v.y << ", " << v.z << ")";
        return os;
    }
};

class SceneObject {
public:
    Vec3f center;
    Real horizontalRadius;
    Real verticalRadius;
    Surface surface;
    Vec3f colour;

    SceneObject(
        const Vec3f &c = Vec3f(),
        const Real &r = 0,
        const Surface &s = Surface::VEGETATION,
        const Vec3f &sc = Vec3f()) :
        center(c),
        horizontalRadius(r),
        verticalRadius(r),
        surface(s),
        colour(sc)
    {}

    SceneObject(
        const Vec3f &c,
        const Real &hr,
        const Real &vr,
        const Surface &s,
        const Vec3f &sc) :
        center(c),
        horizontalRadius(hr),
        verticalRadius(vr),
        surface(s),
        colour(sc)
    {}

    Real directionalDiameter(const Vec3f &direction) const {
        const Real invRh2 = 1.0 / (horizontalRadius * horizontalRadius);
        const Real invRv2 = 1.0 / (verticalRadius * verticalRadius);
        const Real denom = (direction.x * direction.x + direction.y * direction.y) * invRh2 +
                           (direction.z * direction.z) * invRv2;
        if (denom <= 0.0) return 2.0 * std::max(horizontalRadius, verticalRadius);
        return 2.0 / std::sqrt(denom);
    }

    Real relativeThickness(const Real segmentLength, const Vec3f &direction) const {
        const Real diameter = directionalDiameter(direction);
        if (!std::isfinite(diameter) || diameter <= 0.0) return 0.0;
        const Real ratio = segmentLength / diameter;
        return std::max(Real(0.0), std::min(Real(1.0), ratio));
    }

    bool intersect(const Vec3f &rayorig, const Vec3f &raydir, Real &t0, Real &t1) const
    {
        const Vec3f origin = rayorig - center;
        const Real invRh2 = 1.0 / (horizontalRadius * horizontalRadius);
        const Real invRv2 = 1.0 / (verticalRadius * verticalRadius);

        const Real A = (raydir.x * raydir.x + raydir.y * raydir.y) * invRh2 +
                       (raydir.z * raydir.z) * invRv2;
        const Real B = 2.0 * ((origin.x * raydir.x + origin.y * raydir.y) * invRh2 +
                              (origin.z * raydir.z) * invRv2);
        const Real C = ((origin.x * origin.x + origin.y * origin.y) * invRh2 +
                        (origin.z * origin.z) * invRv2) - 1.0;

        if (!std::isfinite(A) || A <= 0.0) return false;

        Real discriminant = B * B - 4.0 * A * C;

        // Scale-aware tolerance: accept tiny negative values caused only by
        // floating-point round-off in tangential intersections.
        const Real discriminantScale = B * B + std::fabs(4.0 * A * C);
        const Real discriminantTolerance =
            32.0 * std::numeric_limits<Real>::epsilon() * discriminantScale;

        if (discriminant < -discriminantTolerance) return false;
        if (discriminant < 0.0) discriminant = 0.0;

        const Real sqrtDiscriminant = std::sqrt(discriminant);

        if (sqrtDiscriminant == 0.0) {
            t0 = t1 = -B / (2.0 * A);
        } else {
            // Numerically stable quadratic solution, avoiding cancellation.
            const Real q = -0.5 * (B + std::copysign(sqrtDiscriminant, B));
            t0 = q / A;
            t1 = C / q;
            if (t0 > t1) std::swap(t0, t1);
        }

        return t1 >= 0.0;
    }

    Vec3f normalAt(const Vec3f &point) const {
        Vec3f normal(
            (point.x - center.x) / (horizontalRadius * horizontalRadius),
            (point.y - center.y) / (horizontalRadius * horizontalRadius),
            (point.z - center.z) / (verticalRadius * verticalRadius)
        );
        const Real len2 = normal.length2();
        if (len2 > 0.0) normal = normal * (1.0 / std::sqrt(len2));
        else normal = Vec3f(0.0, 0.0, 1.0);
        return normal;
    }
};

using Sphere = SceneObject;

inline std::string trim(const std::string& text) {
    const std::string whitespace = " \t\r\n";
    const std::size_t start = text.find_first_not_of(whitespace);
    if (start == std::string::npos) return "";
    const std::size_t end = text.find_last_not_of(whitespace);
    return text.substr(start, end - start + 1);
}

inline SceneObject parseSceneEntry(const std::string& text, const Surface &surface = Surface::VEGETATION, const Vec3f &colour = Vec3f(0.0, 0.0, 0.0)) {
    std::string cleaned = text;
    const std::size_t commentPos = cleaned.find('#');
    if (commentPos != std::string::npos) cleaned = cleaned.substr(0, commentPos);
    cleaned = trim(cleaned);
    if (cleaned.empty()) {
        throw std::invalid_argument("Empty scene entry");
    }

    std::istringstream iss(cleaned);
    std::vector<Real> values;
    Real value;
    while (iss >> value) values.push_back(value);

    if (!iss.eof()) {
        throw std::invalid_argument("Scene entry contains non-numeric data: '" + cleaned + "'");
    }

    if (values.size() != 4 && values.size() != 5) {
        throw std::invalid_argument("Scene entry must contain exactly 4 or 5 numeric values: '" + cleaned + "'");
    }

    for (const Real v : values) {
        if (!std::isfinite(v)) {
            throw std::invalid_argument("Scene entry contains a non-finite value: '" + cleaned + "'");
        }
    }

    const Real x = values[0];
    const Real y = values[1];
    const Real z = values[2];
    const Real horizontalRadius = values[3];
    const Real verticalRadius = (values.size() == 4) ? values[3] : values[4];

    if (horizontalRadius <= 0.0 || verticalRadius <= 0.0) {
        throw std::invalid_argument("Scene entry radii must be strictly positive: '" + cleaned + "'");
    }

    return SceneObject(Vec3f(x, y, z + verticalRadius), horizontalRadius, verticalRadius, surface, colour);
}

struct Camera {
    Real zenith;
    Real azimuth;
    Real distance; // distancia al origen (0,0,0)
    Real fov; // vertical fov en grados
};

struct PixelData {
    Vec3f colour = SKY_COLOUR; // by dafault SKY is reached
    Surface surface = Surface::SKY;
    Real transmission = 0; // if the transmission is not 1, then we are in a shaded area
    Real intersectedDistance = 0; // intersected section of the shaded vegetation
    Real intersectedCrownBaseHeight = 0; // base height of the intersected crown
    Real intersectedCrownHorizontalRadius = 0; // horizontal radius of the intersected crown
    Real reflection = 0;
    Vec3f hittingPosition = 0;
    Real density = 0;
    Real totalDensity = 0;
    Real normalisedTotalDensity = 0;
};

void showProgressBar(unsigned current, unsigned total) {
    const int barWidth = 50;
    double progress = static_cast<double>(current) / total;
    int pos = static_cast<int>(barWidth * progress);

    std::clog << "\r[";
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::clog << "=";
        else if (i == pos) std::clog << ">";
        else std::clog << " ";
    }
    std::clog << "] " << std::fixed << std::setprecision(1) << (progress * 100.0) << "%";
    std::clog.flush();
    if(current == total) std::clog << std::endl << std::flush;
}

PixelData trace(const Vec3f &rayorig, const Vec3f &raydir, const std::vector<Sphere> &spheres) {
    Real tnear = INFINITY;
    const Sphere* sphere = NULL;
    PixelData pixel;

    for (unsigned i = 0; i < spheres.size(); i++) {
        Real t0 = INFINITY, t1 = INFINITY;
        if (spheres[i].intersect(rayorig, raydir, t0, t1)) {
            const Real hitDistance = (t0 >= 0.0) ? t0 : t1;
            const Real segmentStart = std::max(Real(0.0), t0);
            const Real thickness = std::max(Real(0.0), t1 - segmentStart);

            if (hitDistance < tnear) {
                tnear = hitDistance;
                sphere = &spheres[i];
                pixel.density = thickness;
            }
            if(spheres[i].surface == Surface::VEGETATION) {
                const Real relative = spheres[i].relativeThickness(thickness, raydir);
                pixel.totalDensity += thickness;
                pixel.normalisedTotalDensity += relative;
            }
        }
    }

    if (!sphere) return pixel;

    pixel.surface = sphere->surface;
    pixel.density = (sphere->surface == Surface::SOIL) ? 0 : pixel.density;
    pixel.colour = 0;

    Vec3f phit = rayorig + raydir * tnear;
    Vec3f nhit = sphere->normalAt(phit);
    Real bias = 1e-4;

    pixel.hittingPosition = phit;

    for (unsigned i = 0; i < spheres.size(); ++i) {
        if (spheres[i].surface == Surface::LIGHT) {
            Vec3f transmission = 1;
            Vec3f lightDirection = spheres[i].center - phit;
            lightDirection.normalize();

            for (unsigned j = 0; j < spheres.size(); ++j) {
                if (i != j) {
                    Real t0, t1;
                    if (spheres[j].intersect(phit + nhit * bias, lightDirection, t0, t1)) {
                        const Real segmentStart = std::max(Real(0.0), t0);
                        const Real segLen = std::max(Real(0.0), t1 - segmentStart);
                        const Real relative = spheres[j].relativeThickness(segLen, lightDirection);
                        transmission = transmission * (1 - relative);
                        pixel.intersectedDistance += segLen;
                        pixel.intersectedCrownBaseHeight = spheres[j].center.z - spheres[j].verticalRadius;
                        pixel.intersectedCrownHorizontalRadius = spheres[j].horizontalRadius;
                    }
                }
            }

            pixel.colour = pixel.colour + sphere->colour * transmission *
                            std::max(Real(0), nhit.dot(lightDirection));

            pixel.transmission = transmission.x;
            pixel.reflection = std::max(Real(0), nhit.dot(lightDirection));
        }
    }

    return pixel;
}

std::vector<PixelData> render(const std::vector<Sphere> &spheres, const Camera& camera)
{
    std::vector<PixelData> pixelDataArray(width * height);
    Real invWidth = 1 / Real(width), invHeight = 1 / Real(height);
    Real aspectratio = width / Real(height);
    Real angle = tan(M_PI * 0.5 * camera.fov / 180.);
    
    // Camera configuration and position
    Real cameraZenithRad = (camera.zenith == 0 ? 0.00001 : camera.zenith) * M_PI / 180.0f;
    Real cameraAzimuthRad = camera.azimuth * M_PI / 180.0f;
    Real camX = camera.distance * sin(cameraZenithRad) * sin(cameraAzimuthRad);
    Real camY = camera.distance * sin(cameraZenithRad) * cos(cameraAzimuthRad);
    Real camZ = camera.distance * cos(cameraZenithRad);
    Vec3f cameraPosition(camX, camY, camZ);
    Vec3f cameraTarget(0.0f, 0.0f, 0.0f);
    Vec3f cameraDirection = (cameraTarget - cameraPosition).normalize();
	
    Vec3f upVector(0.0f, 0.0f, 1.0f);
    Vec3f cameraRight = cameraDirection.cross(upVector).normalize();
    Vec3f cameraUp = cameraRight.cross(cameraDirection).normalize();
    
    #pragma omp parallel for schedule(dynamic) collapse(1)
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            Real pixelNDCX = (x + 0.5f) * invWidth;
            Real pixelNDCY = (y + 0.5f) * invHeight;

            Real pixelScreenX = 2 * pixelNDCX - 1;
            Real pixelScreenY = 1 - 2 * pixelNDCY;

            Real pixelCameraX = pixelScreenX * angle * aspectratio;
            Real pixelCameraY = pixelScreenY * angle;

            Vec3f rayDirection = (cameraDirection +
                                  cameraRight * pixelCameraX +
                                  cameraUp * pixelCameraY).normalize();

            PixelData pixelData = trace(cameraPosition, rayDirection, spheres);
            pixelDataArray[y * width + x] = pixelData;

        }
    }
    return pixelDataArray;
}

Real solarRadius(Real solarDistance) {
    Real solarRadiusReal = 696340.0;      // Radio del Sol en km
    Real solarDistanceReal = 149600000.0; // Distancia Tierra-Sol en km
    Real solarAngleRad = 2 * atan(solarRadiusReal / solarDistanceReal);
    
	return(solarDistance * tan(solarAngleRad / 2.0));
}

std::vector<Sphere> loadScene(const std::string& filename, Sphere sun, const Real heightOffset = 0.0f) {
    std::vector<Sphere> spheres;
    std::ifstream file(filename);
    std::string line;

    // Three green hues
    std::vector<Vec3f> greenTones = {
        Vec3f(102 / 255.0f, 194 / 255.0f, 164 / 255.0f), // #66c2a4
        Vec3f(44 / 255.0f, 162 / 255.0f, 95 / 255.0f),   // #2ca25f
        Vec3f(0 / 255.0f, 109 / 255.0f, 44 / 255.0f)     // #006d2c
    };

    int colorIndex = 0;

    if (file.is_open()) {
        if(debug == 2) std::clog << "SCENE: " << std::endl;
        while (getline(file, line)) {
            if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
            const std::size_t commentStart = line.find('#');
            const std::string trimmedLine = (commentStart == std::string::npos) ? line : line.substr(0, commentStart);
            if (trimmedLine.find_first_not_of(" \t\r\n") == std::string::npos) continue;

            try {
                const SceneObject object = parseSceneEntry(trimmedLine, Surface::VEGETATION, greenTones[colorIndex % greenTones.size()]);
                colorIndex++;
                spheres.push_back(object);
                if(debug == 2) {
                    std::clog << "\t(" << object.center.x << ", " << object.center.y << ", " << object.center.z << ")\trh = " << object.horizontalRadius << ", rv = " << object.verticalRadius << "\t" << object.colour << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error reading the line (" << line << "): " << e.what() << std::endl;
                std::exit(EXIT_FAILURE);
            }
        }
        file.close();
    } else {
        std::cerr << "Scene file not found: " << filename << std::endl;
        std::exit(EXIT_FAILURE);
    }

    spheres.push_back(Sphere(Vec3f( 0, 0, -SOIL_SPHERE_RADIUS), SOIL_SPHERE_RADIUS + heightOffset, Surface::SOIL, SOIL_COLOUR)); // Suelo
    spheres.push_back(sun);

    return spheres;
}

Sphere generateSun(const Settings& settings) {
    Real solarZenith = settings.sza;
    Real solarAzimuth = settings.saa;
        
    // Calculate the position of the sun at x, y, z
    Real solarDistance = settings.cds*1.001;  // Distance from the origin, must be behind the camera
    Real solarZenithRad = solarZenith * M_PI / 180.0f;
    Real solarAzimuthRad = solarAzimuth * M_PI / 180.0f;

    Real sunX = solarDistance * sin(solarZenithRad) * sin(solarAzimuthRad);
    Real sunY = solarDistance * sin(solarZenithRad) * cos(solarAzimuthRad);
    Real sunZ = solarDistance * cos(solarZenithRad);
    Vec3f sunPosition(sunX, sunY, sunZ);

    // Calculation of the sun radius based on the subtended angle (angular diameter)
    Real sunRadius = solarRadius(solarDistance);
    
    if(debug) std::clog << "Sun position: (" << sunX << ", " << sunY << ", " << sunZ << ") r: " << sunRadius << "" << std::endl << std::flush;
    
    return Sphere(sunPosition, sunRadius, Surface::LIGHT, 0);
}

Settings loadSettings(const std::string& filename, const Settings& preferedSettings, bool &gap_fraction_profile) {
    std::ifstream file(filename);
    std::string line;

    Settings settings;

    if (!file.is_open()) {
        std::cerr << "Cannot open settings file: " << filename << std::endl;
        std::exit(EXIT_FAILURE);
    }

    while (getline(file, line)) {
        // Ignore empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Find equal symbol
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue; // Saltar si no se encuentra '='

        // Extract key and value
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // Clean it
        key.erase(key.find_last_not_of(" \t") + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));

        Real floatValue = std::stof(value);

        if (key == "camera_zenith") {
            settings.cza = std::isnan(preferedSettings.cza) ? floatValue : preferedSettings.cza;
        } else if (key == "camera_azimuth") {
            settings.caa = std::isnan(preferedSettings.caa) ? floatValue : preferedSettings.caa;
        } else if (key == "camera_distance") {
            settings.cds = std::isnan(preferedSettings.cds) ? floatValue : preferedSettings.cds;
        } else if (key == "camera_fov") {
            settings.fov = std::isnan(preferedSettings.fov) ? floatValue : preferedSettings.fov;
        } else if (key == "solar_zenith") {
            settings.sza = std::isnan(preferedSettings.sza) ? floatValue : preferedSettings.sza;
        } else if (key == "solar_azimuth") {
            settings.saa = std::isnan(preferedSettings.saa) ? floatValue : preferedSettings.saa;
        } else if (key == "debug") {
            debug = (unsigned) floatValue;
        } else if (key == "gap_fraction_profile_enabled") {
            gap_fraction_profile = (bool) floatValue;
        }
    }
    file.close();
    
    if(debug) {
        std::clog << "Debug mode enabled" << std::endl << std::flush;
        std::clog << "settings_file='" << filename << "'" << std::endl << std::flush;
        std::clog << "camera_zenith=" << settings.cza << ", camera_azimuth=" << settings.caa << ", camera_distance=" << settings.cds << ", camera_fov=" << settings.fov << std::endl << std::flush;
        std::clog << "solar_zenith=" << settings.sza << ", solar_azimuth=" << settings.saa << std::endl << std::flush;
        std::clog << "gap_fraction_profile_enabled=" << gap_fraction_profile << std::endl << std::flush;
    }

    return settings;
}

Settings processArgs(int argc, char **argv, std::string& sceneFile, std::string& configFile) {
    int opt;
    Settings settings = {NAN, NAN, NAN, NAN, NAN, NAN}; // para luego controlar qué se ha cambiado

    struct option long_options[] = {
        {"scene", required_argument, 0, 's'},   // --scene or -s
        {"config", required_argument, 0, 'c'},  // --config or -c
        {"help", no_argument, 0, 'h'},          // --help or -h
        {"sza", required_argument, 0, 'z'},     // --sza (-z deshabilitado)
        {"saa", required_argument, 0, 'a'},     // --saa (-a deshabilitado)
        {"cza", required_argument, 0, 'm'},     // --cza (-m deshabilitado)
        {"caa", required_argument, 0, 'n'},     // --caa (-n deshabilitado)
        {"cds", required_argument, 0, 'd'},     // --cds (-d deshabilitado)
        {"fov", required_argument, 0, 'f'},     // --fov (-f deshabilitado)
        {0, 0, 0, 0}                            // Termino de la lista
    };

    while ((opt = getopt_long(argc, argv, "s:c:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 's':
                sceneFile = optarg;
                break;
            case 'c':
                configFile = optarg;
                break;
            case 'h':
                std::cout << "Usage: " << argv[0] << std::endl << 
                    "\t[-s|--scene scene_file (scene.txt)]" << std::endl << 
                    "\t[-c|--config config_file (settings.txt)]" << std::endl << 
                    "\t[--sza value] [--saa value] [--cza value] [--caa value] [--cds value] [--fov value]" << std::endl << 
                    "\t[-h] (show this)" << std::endl << std::flush;
                exit(EXIT_SUCCESS);
            case 'z':
                settings.sza = std::stod(optarg);
                break;
            case 'a':
                settings.saa = std::stod(optarg);
                break;
            case 'm':
                settings.cza = std::stod(optarg);
                break;
            case 'n':
                settings.caa = std::stod(optarg);
                break;
            case 'd':
                settings.cds = std::stod(optarg);
                break;
            case 'f':
                settings.fov = std::stod(optarg);
                break;
            default:
                std::cerr << "Use -h for help!\n";
                exit(EXIT_FAILURE);
        }
    }

    return settings;
}

void saveImage(const std::vector<PixelData>& pixelDataArray, const std::string& filename) {
    std::ofstream asciiImage("./output_image.ppm", std::ios::out | std::ios::binary);
    asciiImage << "P6\n" << width << " " << height << "\n255\n";
    
    // Save the result in a BIP file containing all the generated output
    std::ofstream bsqFile("./output.bip", std::ios::out | std::ios::binary | std::ios::trunc);
    if (!bsqFile) {
        std::cerr << "Error opening BIP file for writing" << std::endl;
        return;
    }

    for (unsigned i = 0; i < width * height; ++i) {
        const PixelData& pixelData = pixelDataArray[i];

        // 11-band file
        float values[15]; // necesario por el uso de Real en lugar de float
        values[0] = static_cast<float>(pixelData.colour.x);
        values[1] = static_cast<float>(pixelData.colour.y);
        values[2] = static_cast<float>(pixelData.colour.z);
        values[3] = static_cast<float>(pixelData.surface);
        values[4] = static_cast<float>(pixelData.transmission);
        values[5] = static_cast<float>(pixelData.intersectedDistance);
        values[6] = static_cast<float>(pixelData.intersectedCrownBaseHeight);
        values[7] = static_cast<float>(pixelData.intersectedCrownHorizontalRadius);
        values[8] = static_cast<float>(pixelData.reflection);
        values[9] = static_cast<float>(pixelData.hittingPosition.x);
        values[10] = static_cast<float>(pixelData.hittingPosition.y);
        values[11] = static_cast<float>(pixelData.hittingPosition.z);
        values[12] = static_cast<float>(pixelData.density);
        values[13] = static_cast<float>(pixelData.totalDensity);
        values[14] = static_cast<float>(pixelData.normalisedTotalDensity);

        bsqFile.write(reinterpret_cast<const char*>(values), sizeof(float) * 15);

        asciiImage << (unsigned char)(std::min(float(1), values[0]) * 255) <<
                      (unsigned char)(std::min(float(1), values[1]) * 255) <<
                      (unsigned char)(std::min(float(1), values[2]) * 255);
    }
    asciiImage.close();
    bsqFile.close();

    // header file for the BIP output
    std::ofstream hdrFile("./output.hdr", std::ios::out | std::ios::trunc);
    if (!hdrFile) {
        std::cerr << "Error writing HDR header file" << std::endl;
        return;
    }

    hdrFile << "ENVI\n";
    hdrFile << "samples = " << width << "\n";
    hdrFile << "lines = " << height << "\n";
    hdrFile << "bands = 15\n";
    hdrFile << "header offset = 0\n";
    hdrFile << "file type = ENVI Standard\n";
    hdrFile << "data type = 4\n"; // 4 bytes (IEEE)
    hdrFile << "interleave = bip\n"; // Interleaving BIP
    hdrFile << "byte order = 0\n"; // Byte order: 0 means little-endian
    hdrFile << "band names = { Red, Green, Blue, \
                Surface [soil(0) or vegetation(1)], Transmission [0 (shadow) to 1 (sunlit)], \
                Intersected Distance (m), Intersected Crown Base Height (m), Intersected Crown Horizontal Radius (m), \
                Reflection [0-1], HitX, HitY, HitZ, Density (m), TotalDensity (m), NormalisedTotalDensity [0-1+ w/ overlapping] }\n";

    hdrFile.close();
}

Stats computeStats(const std::vector<PixelData>& pixelDataArray) {
    Stats stats{};

    unsigned n_pixels_soil_sunlit = 0;
    unsigned n_pixels_soil_shaded = 0;
    unsigned n_pixels_vegetation_sunlit = 0;
    unsigned n_pixels_vegetation_shaded = 0; 
    unsigned n_pixels_total;
    Real accumulated_normalised_density = 0;
    
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            if(pixelDataArray[y * width + x].surface == Surface::SOIL) {
                if(pixelDataArray[y * width + x].transmission == 1) n_pixels_soil_sunlit++;
                else n_pixels_soil_shaded++;
            } else if(pixelDataArray[y * width + x].surface == Surface::VEGETATION) {
                accumulated_normalised_density += pixelDataArray[y * width + x].normalisedTotalDensity;
                if(pixelDataArray[y * width + x].transmission == 1) n_pixels_vegetation_sunlit++;
                else n_pixels_vegetation_shaded++;
            }
        }
    }

    n_pixels_total = n_pixels_soil_shaded + n_pixels_soil_sunlit + n_pixels_vegetation_shaded + n_pixels_vegetation_sunlit;

    const unsigned n_pixels_sky = width * height - n_pixels_total;
    if (debug && n_pixels_sky > 0) {
        std::clog << "Sky pixels excluded from statistics: " << n_pixels_sky 
            << " / " << width * height << " (" << std::fixed << std::setprecision(2)
            << 100.0 * static_cast<Real>(n_pixels_sky) / (width * height) << "%)" << std::endl;
    }

    if (n_pixels_total == 0) {
        std::clog << "Warning: no soil or vegetation pixels were found" << std::endl;
        return stats;
    }

    stats.sunlit_soil = (Real) n_pixels_soil_sunlit / n_pixels_total;
    stats.shaded_soil = (Real) n_pixels_soil_shaded / n_pixels_total;
    stats.sunlit_vegetation = (Real) n_pixels_vegetation_sunlit / n_pixels_total;
    stats.shaded_vegetation = (Real) n_pixels_vegetation_shaded / n_pixels_total;
    
    const unsigned n_pixels_vegetation =
        n_pixels_vegetation_shaded + n_pixels_vegetation_sunlit;
    constexpr Real MEAN_CHORD_NORMALISATION = 1.5; // equivalent to the previous /4*6
    stats.normalised_density =
        (n_pixels_vegetation > 0)
        ? accumulated_normalised_density / n_pixels_vegetation * MEAN_CHORD_NORMALISATION
        : 0.0;

    return stats;
}

void showStats(const std::vector<PixelData>& pixelDataArray) {
    Stats stats = computeStats(pixelDataArray);

    std::cout << "gap_fraction = " << stats.shaded_soil + stats.sunlit_soil << std::endl;
    std::cout << "sunlit_soil = " << stats.sunlit_soil << std::endl;
    std::cout << "shaded_soil = " << stats.shaded_soil << std::endl;
    std::cout << "sunlit_vegetation = " << stats.sunlit_vegetation << std::endl;
    std::cout << "shaded_vegetation = " << stats.shaded_vegetation << std::endl << std::flush;
    std::cout << std::setprecision(4) << "normalised_density = " << stats.normalised_density << std::endl << std::flush;
}

void createVerticalProfile(const std::vector<PixelData>& pixelDataArray) {
    std::vector<Real> rowDensities(height, 0.0f);
    std::vector<Real> heights(height, 0.0f);

    for (unsigned y = 0; y < height; ++y) {
        Real heightSum = 0.0f;
        unsigned count = 0;

        for (unsigned x = 0; x < width; ++x) {
            rowDensities[y] += pixelDataArray[y * width + x].totalDensity;

            Real currentHeight = pixelDataArray[y * width + x].hittingPosition.z;
            if (currentHeight > 0) {
                heightSum += currentHeight;
                count++;
            }
        }
        // Calculate the average height for row y
        heights[y] = (count > 0) ? (heightSum / count) : 0.0f;  // Avoid dividing by zero
    }

    std::ofstream profileFile("./vertical_profile.csv");
    if (!profileFile) {
        std::cerr << "Error al abrir profile.txt para escritura." << std::endl;
    } else {
        profileFile << "density, height" << std::endl;
        for (unsigned y = 0; y < height; ++y) {
            if(rowDensities[y] > 0.0f)
                profileFile << rowDensities[y] << "," << heights[y] << std::endl;
        }
        profileFile.close();
    }
}
int main(int argc, char **argv)
{
    std::string sceneFile = "scene.txt";
    std::string configFile = "settings.txt";
    bool gap_fraction_profile = false;

    // Arguments take precedence over configuration files
    Settings settingsFromArgs = processArgs(argc, argv, sceneFile, configFile);
    Settings settings = loadSettings(configFile, settingsFromArgs, gap_fraction_profile);

    Camera camera = { settings.cza, settings.caa, settings.cds, settings.fov };
    Sphere sun = generateSun(settings);
    std::vector<Sphere> scenario = loadScene(sceneFile, sun);
    std::vector<PixelData> pixelDataArray = render(scenario, camera);
    
    saveImage(pixelDataArray, "output_image");
    showStats(pixelDataArray);
    
    if(camera.zenith == 90)
        createVerticalProfile(pixelDataArray);
    else if(gap_fraction_profile) {
        std::ofstream gfpFile("./gap_fraction_profile.csv");

        if(debug)
            std::clog << "Starting gap fraction profile..." << std::endl << std::flush;
        
Real max_height = 0.0;
        for (const Sphere& object : scenario) {
            if (object.surface == Surface::VEGETATION) {
                max_height = std::max(max_height, object.center.z + object.verticalRadius);
            }
        }
        max_height = std::ceil(max_height);

        gfpFile << "height,all_veg, sunlit_veg, shaded_veg, all_soil, sunlit_soil, shaded_soil" << std::endl;

        if (max_height <= 0.0) {
            if (debug)
                std::clog << "Gap fraction profile skipped: no vegetation height available." << std::endl;
        } else {
            const Real dz = max_height / PROFILE_STEPS;
            for (unsigned step = 0; step <= PROFILE_STEPS; ++step) {
                const Real z = step * dz;

                // para cada z, calculamos el gap_fraction y su proporcion sunlit/shaded
                scenario = loadScene(sceneFile, sun, z);
                std::vector<PixelData> pd = render(scenario, camera);
                Stats stats = computeStats(pd);

                gfpFile << z << "," << stats.sunlit_vegetation + stats.shaded_vegetation << "," <<
                    stats.sunlit_vegetation << "," << stats.shaded_vegetation << "," <<
                    stats.sunlit_soil + stats.shaded_soil << "," << stats.sunlit_soil << "," << stats.shaded_soil << std::endl;

                if(debug)
                    showProgressBar(step, PROFILE_STEPS);
            }
        }
        gfpFile.close();
    }
    exit(EXIT_SUCCESS);
}