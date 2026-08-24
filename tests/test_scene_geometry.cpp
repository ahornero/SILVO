#define main SILVO_main
#include "../src/silvo.cpp"
#undef main

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    {
        // Legacy 4-column format must still behave like a sphere with its centre placed at z + radius.
        const auto sphere = parseSceneEntry("0 0 0 2.5");
        assert(std::isfinite(sphere.horizontalRadius));
        assert(sphere.horizontalRadius > 0.0);
        assert(sphere.verticalRadius > 0.0);
        assert(sphere.horizontalRadius == sphere.verticalRadius);
        assert(sphere.center.z == 2.5);
    }

    {
        // 5-column entries are the new ellipsoid format: horizontal and vertical radii are independent.
        const auto spheroid = parseSceneEntry("1 2 3 4 6");
        assert(std::isfinite(spheroid.horizontalRadius));
        assert(std::isfinite(spheroid.verticalRadius));
        assert(spheroid.horizontalRadius == 4.0);
        assert(spheroid.verticalRadius == 6.0);
        assert(spheroid.center.z == 9.0);
    }

    {
        // A ray going along +Z through a vertical spheroid must hit the crown and produce a positive interval.
        const Vec3f origin(0, 0, -10);
        const Vec3f dir(0, 0, 1);
        const SceneObject obj(Vec3f(0, 0, 3), 2.0, 4.0, Surface::VEGETATION, Vec3f(0, 1, 0));
        Real t0 = 0, t1 = 0;
        const bool hit = obj.intersect(origin, dir, t0, t1);
        assert(hit);
        assert(t0 > 0.0);
        assert(t1 > t0);
    }

    {
        // For a sphere centred at the origin, a ray crossing the full diameter should produce a segment length of 4.
        const Vec3f origin(-10, 0, 0);
        const Vec3f dir(1, 0, 0);
        const SceneObject obj(Vec3f(0, 0, 0), 2.0, 2.0, Surface::VEGETATION, Vec3f(0, 1, 0));
        Real t0 = 0, t1 = 0;
        assert(obj.intersect(origin, dir, t0, t1));
        assert(std::fabs((t1 - t0) - 4.0) < 1e-6);
    }

    {
        // A ray starting beyond the sphere must miss without producing a valid intersection.
        const Vec3f origin(10, 0, 0);
        const Vec3f dir(1, 0, 0);
        const SceneObject obj(Vec3f(0, 0, 0), 2.0, 2.0, Surface::VEGETATION, Vec3f(0, 1, 0));
        Real t0 = 0, t1 = 0;
        assert(!obj.intersect(origin, dir, t0, t1));
    }

    {
        // The normal on the equator of a vertical spheroid should lie in the horizontal plane and remain unit length.
        const SceneObject obj(Vec3f(0, 0, 0), 2.0, 4.0, Surface::VEGETATION, Vec3f(0, 1, 0));
        const Vec3f eq = obj.normalAt(Vec3f(0, 2, 0));
        assert(std::fabs(eq.x) < 1e-9);
        assert(std::fabs(eq.y) > 0.0);
        assert(std::fabs(eq.z) < 1e-9);
        assert(std::fabs(eq.length() - 1.0) < 1e-9);
    }

    {
        // Inline comments after numeric tokens must be ignored without changing the parsed geometry.
        const auto withComment = parseSceneEntry("0 0 0 2.5 # trailing comment");
        assert(withComment.center.z == 2.5);
        assert(withComment.verticalRadius == 2.5);
    }

    {
        // Mixed 4/5-column inputs are intentionally supported for backward compatibility and new spheroid usage.
        const auto mixed = parseSceneEntry("1 2 3 4 5");
        assert(mixed.horizontalRadius == 4.0);
        assert(mixed.verticalRadius == 5.0);
    }

    {
        // Invalid radii must be rejected early, before they can affect the ray tracing.
        bool rejected = false;
        try {
            parseSceneEntry("1 2 3 0");
        } catch (const std::exception&) {
            rejected = true;
        }
        assert(rejected);
    }

    {
        // Invalid vertical radius in the 5-column format must also fail validation.
        bool rejected = false;
        try {
            parseSceneEntry("1 2 3 4 0");
        } catch (const std::exception&) {
            rejected = true;
        }
        assert(rejected);
    }

    std::cout << "scene geometry checks passed" << std::endl;
    return 0;
}
