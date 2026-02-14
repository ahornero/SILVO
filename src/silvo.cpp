/*
 * SILVO – Simplified Light–Vegetation Overlay Model
 * --------------------------------------------------
 * File: raytracer.cpp
 *
 * Description:
 *   Core ray-tracing module of the SILVO model. This component implements
 *   backward (eye) ray tracing to simulate geometric illumination within
 *   heterogeneous vegetation canopies represented as collections of spheres.
 *   The model computes structural metrics such as gap fraction, transmission,
 *   reflection and cumulative vegetation path length, and produces a simple
 *   rendered scene for visual interpretation and a BIP file for analysis.
 *
 * Author:
 *   Alberto Hornero (© 2025)
 *   Institute of Sustainable Agriculture (IAS–CSIC), Spain
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
 *   Hornero. A, et al. (2025). SILVO: A lightweight 3D illumination
 *   model to characterise the spatial structure of heterogeneous vegetation
 *   canopies. (Accepted, DOI pending).
 *
 * --------------------------------------------------------------------------
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

class Sphere {
public:
    Vec3f center;			// Sphere position
    Real radius, radius2;	// Sphere definition
    Surface surface;        // Surface type
    Vec3f colour;     		// Surface colour

    Sphere(
        const Vec3f &c,
        const Real &r,
        const Surface &s,
        const Vec3f &sc) :
        center(c), radius(r), radius2(r * r), surface(s), colour(sc)
    {}
    
    // Calculate the intersection of a ray with the sphere using a geometric solution
    bool intersect(const Vec3f &rayorig, const Vec3f &raydir, Real &t0, Real &t1) const
    {
        Vec3f l = center - rayorig;
        Real tca = l.dot(raydir);
        if (tca < 0) return false;
        Real d2 = l.dot(l) - tca * tca;
        if (d2 > radius2) return false;
        Real thc = sqrt(radius2 - d2);
        t0 = tca - thc;
        t1 = tca + thc;
        
        return true;
    }
};

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
    Real intersectedSphereHeight = 0; // idem for height
    Real intersectedSphereRadius = 0; // idem for radius
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

    // Find the closest intersection of the beam with the spheres
    for (unsigned i = 0; i < spheres.size(); i++) {
        Real t0 = INFINITY, t1 = INFINITY;
        if (spheres[i].intersect(rayorig, raydir, t0, t1)) {
            if (t0 < 0) t0 = t1;
            if (t0 < tnear) {
                tnear = t0;
                sphere = &spheres[i];
                pixel.density = t1 - t0;
            }
            if(spheres[i].surface == Surface::VEGETATION) {
                pixel.totalDensity += (t1 - t0);
                pixel.normalisedTotalDensity += (t1 - t0) / (spheres[i].radius * 2);
            }
        }
    }

    // If there is no intersection, returns the default pixel.
    if (!sphere) return pixel;

    pixel.surface = sphere->surface;
    pixel.density = (sphere->surface == Surface::SOIL) ? 0 : pixel.density;
    pixel.colour = 0;
    
    // Point of the first intersection
    Vec3f phit = rayorig + raydir * tnear;
    Vec3f nhit = phit - sphere->center;
    nhit.normalize();
    Real bias = 1e-4;

    pixel.hittingPosition = phit;

    // Contribution of the light sources to the final colour of the object
    for (unsigned i = 0; i < spheres.size(); ++i) {
        if (spheres[i].surface == Surface::LIGHT) {
            Vec3f transmission = 1;
            Vec3f lightDirection = spheres[i].center - phit;
            lightDirection.normalize();
            
            // Check if the point is shaded
            for (unsigned j = 0; j < spheres.size(); ++j) {
                if (i != j) {
                    Real t0, t1;
                    if (spheres[j].intersect(phit + nhit * bias, lightDirection, t0, t1)) {
                        transmission = transmission * (1 - (t1 - t0) / (spheres[j].radius * 2)); // se calcula en función del grosor interceptado
                        pixel.intersectedDistance += (t1 - t0);
                        pixel.intersectedSphereHeight = spheres[j].center.z - spheres[j].radius;
                        pixel.intersectedSphereRadius = spheres[j].radius;
                    }
                }
            }
            
            // Accumulates light colour if not shaded
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
    Real x, y, z, radius;

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
            // Skip commented lines
            if (line.empty() || line[0] == '#')
                continue;

            std::istringstream iss(line);            
            if (iss >> x >> y >> z >> radius) {
                // Assign colour cyclically
                Vec3f color = greenTones[colorIndex % greenTones.size()];
                colorIndex++;

                // Offset to the Z-coordinate so that the sphere starts on the ground
                z += radius;
                // Add the sphere with the assigned colour
                spheres.push_back(Sphere(Vec3f(x, y, z), radius, Surface::VEGETATION, color));

                if(debug == 2) std::clog << "\t(" << x << ", " << y << ", " << z << ")\tr = " << radius << "\t" << color << std::endl;
            } else {
                std::cerr << "Error reading the line: " << line << std::endl;
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
        values[6] = static_cast<float>(pixelData.intersectedSphereHeight);
        values[7] = static_cast<float>(pixelData.intersectedSphereRadius);
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
                Intersected Distance (m), Intersected Sphere Height (m), Intersected Sphere Radius (m), \
                Reflection [0-1], HitX, HitY, HitZ, Density (m), TotalDensity (m), NormalisedTotalDensity [0-1+ w/ overlapping] }\n";

    hdrFile.close();
}

Stats computeStats(const std::vector<PixelData>& pixelDataArray) {
    Stats stats;

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

    if(n_pixels_total != width * height)
        std::clog << "Warning: n_pixels_total != width * height (" << n_pixels_total << " != " << width * height << ")" << std::endl;

    stats.sunlit_soil = (Real) n_pixels_soil_sunlit / n_pixels_total;
    stats.shaded_soil = (Real) n_pixels_soil_shaded / n_pixels_total;
    stats.sunlit_vegetation = (Real) n_pixels_vegetation_sunlit / n_pixels_total;
    stats.shaded_vegetation = (Real) n_pixels_vegetation_shaded / n_pixels_total;
    stats.normalised_density = accumulated_normalised_density / (n_pixels_vegetation_shaded + n_pixels_vegetation_sunlit) /4 * 6;

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
        
        Real max_height = 0;
        for (unsigned x = 0; x < height*width; ++x)
            if (pixelDataArray[x].hittingPosition.z > max_height)
                max_height = pixelDataArray[x].density;
        max_height = ceil(max_height);
        
        // for loop from 0 to max_height with 50 steps
        gfpFile << "height,all_veg, sunlit_veg, shaded_veg, all_soil, sunlit_soil, shaded_soil" << std::endl;
        unsigned count = 0;
        for (Real z = 0; z <= max_height; z += max_height / PROFILE_STEPS) {
            // para cada z, calculamos el gap_fraction y su proporcion sunlit/shaded
            scenario = loadScene(sceneFile, sun, z);
            std::vector<PixelData> pd = render(scenario, camera);
            Stats stats = computeStats(pd);

            gfpFile << z << "," << stats.sunlit_vegetation + stats.shaded_vegetation << "," << 
                stats.sunlit_vegetation << "," << stats.shaded_vegetation << "," <<
                stats.sunlit_soil + stats.shaded_soil << "," << stats.sunlit_soil << "," << stats.shaded_soil << std::endl;

            if(debug)
                showProgressBar(count++, PROFILE_STEPS);
        }
        gfpFile.close();
    }
    exit(EXIT_SUCCESS);
}