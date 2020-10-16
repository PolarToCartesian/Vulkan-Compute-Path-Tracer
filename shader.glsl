#version 440

// User defined #defines
#define WORKGROUP_SIZE (32)       // The Number Of Threads Per Workgroup (NVIDIA: 32, AMD: 64)
#define WIDTH          (1920 / 2) // The Target Surface's Width  In Pixels
#define HEIGHT         (1080 / 2) // The Target Surface's Height In Pixels
#define SPP_COUNT      (100)      // Number Of Path Simulations Per Pixel
#define MAX_ITERATIONS (10)       // The Maximum Number Of Iterations For Each Sample

// Raw constant #defines
#define FLT_MAX (3.402823466e+38) // Highest 32-bit Floating Point Number Possible
#define EPSILON (0.001f)          // A Very Small Value
#define M_PI    (3.1415926535897) // π
#define RAND_NUMBER_COUNT (100)
#define TWO_PI_INV (1.f / (2.f * M_PI))
#define PROBABILITY_OF_NEW_RAY (1.f / (2 * 3.141592f))

// Shader Inputs
layout (local_size_x = WORKGROUP_SIZE, local_size_y = WORKGROUP_SIZE, local_size_z = 1 ) in;
layout (std140, binding = 0) buffer buf { vec4 pixelBuffer[]; };

// Misc Constants
const vec3 skyDarkBlue  = vec3(53,  214, 237) / 255.f;
const vec3 skyLightBlue = vec3(201, 246, 255) / 255.f;

const float cameraFOV = M_PI / 5;
const float cameraProjectH = tan(cameraFOV);
const float cameraProjectW = cameraProjectH * float(WIDTH) / float(HEIGHT);

// Random Number Generation
// It's faster to use predefined random numbers from my testing
// as the speed of generation and "randomness" is improved
uint randomNumberIndex = 0; // Index To Next Random Number
const float[RAND_NUMBER_COUNT] randomNumbers = { 0.199597f, 0.604987f, 0.255558f, 0.421514f, 0.720092f, 0.815522f, 0.192279f, 0.385067f, 0.350586f, 0.397595f, 0.357564f, 0.748578f, 0.00414681f, 0.533777f, 0.995393f, 0.907929f, 0.494525f, 0.472084f, 0.864498f, 0.695326f, 0.938409f, 0.785484f, 0.290453f, 0.13312f, 0.943201f, 0.926033f, 0.320409f, 0.0662487f, 0.25414f, 0.421945f, 0.667499f, 0.444524f, 0.838885f, 0.908202f, 0.8063f, 0.291879f, 0.114376f, 0.875398f, 0.247916f, 0.045868f, 0.535327f, 0.491882f, 0.642606f, 0.184197f, 0.154249f, 0.14628f, 0.939923f, 0.979867f, 0.503506f, 0.478285f, 0.491597f, 0.0545161f, 0.847528f, 0.0108021f, 0.934526f, 0.282655f, 0.0207591f, 0.329495f, 0.328761f, 0.560112f, 0.119835f, 0.296947f, 0.289384f, 0.83466f, 0.164883f, 0.0987901f, 0.0792031f, 0.258547f, 0.0754077f, 0.0143626f, 0.318207f, 0.483693f, 0.0715536f, 0.998425f, 0.322974f, 0.879418f, 0.261024f, 0.49866f, 0.453179f, 0.347203f, 0.638452f, 0.274543f, 0.595394f, 0.640481f, 0.798533f, 0.680735f, 0.95186f, 0.4518f, 0.969803f, 0.419822f, 0.00485671f, 0.727772f, 0.475605f, 0.816288f, 0.55194f, 0.550753f, 0.601672f, 0.908048f, 0.35448f, 0.863961f };

float RandomFloat() {
  const float result = randomNumbers[randomNumberIndex % RAND_NUMBER_COUNT];
  randomNumberIndex++;
  return result;
}

vec3 RandomVec3InUnitSphere() {
  const vec3 result = vec3(2 * randomNumbers[randomNumberIndex       % RAND_NUMBER_COUNT] - 1.0f,
                           2 * randomNumbers[(randomNumberIndex + 1) % RAND_NUMBER_COUNT] - 1.0f,
                           2 * randomNumbers[(randomNumberIndex + 2) % RAND_NUMBER_COUNT] - 1.0f);
  randomNumberIndex += 3;
  return result;
}

// Custom Datatypes
struct Material {
  vec4 diffuseColor;
  vec4 emittance;
};

struct Sphere {
  vec3  center;
  float radius;
  Material material;
};

struct Plane {
  vec3 point;        // any point on the plane
  vec3 normal;       // the plane's normal
  Material material; // the plane's material
};

struct Ray {
  vec3 origin;    // the ray's origin
  vec3 direction; // the ray's normalized direction
};

struct Intersection {
  vec3     inDirection; // the direction of the incoming ray
  float    t;           // distance from the ray's origin to intersection point
  vec3     location;    // location of intersection
  vec3     normal;      // the normal at the intersection
  Material material;    // the material that the intersected object is made of
};

#define NULL_RAY          (Ray(vec3(0.0f), vec3(0.0f)))
#define NULL_INTERSECTION (Intersection(vec3(0.f), FLT_MAX, vec3(0.f), vec3(0.f), Material(vec4(0.f), vec4(0.f))))

Sphere[2] spheres = {
  Sphere(vec3(0.00f, 0.f, 2.f), 0.5f, Material(vec4(1.f, 0.f, 0.f, 0.f), vec4(0.f, 0.f, 0.f, 0.f))),
  Sphere(vec3(1.25f, 0.f, 1.f), 0.5f, Material(vec4(0.f, 0.f, 0.f, 0.f), vec4(1.f, 1.f, 1.f, 0.f)))
};

Intersection Intersects(const Ray ray, const Sphere sphere) {
  const vec3  L   = sphere.center - ray.origin;
  const float tca = dot(L, ray.direction);
  const float d2  = dot(L, L) - tca * tca;

  if (d2 > sphere.radius)
    return NULL_INTERSECTION;
  
  const float thc = sqrt(sphere.radius - d2);
  float t0 = tca - thc;
  float t1 = tca + thc;

  if (t0 > t1) {
    const float tmp = t0;
    t0 = t1;
    t1 = tmp;
  }

  if (t0 < EPSILON) {
    t0 = t1;

    if (t0 < 0)
      return NULL_INTERSECTION;
  }

  Intersection intersection;
  intersection.t           = t0;
  intersection.location    = ray.origin + t0 * ray.direction;
  intersection.normal      = normalize(intersection.location - sphere.center);
  intersection.material    = sphere.material;
  intersection.inDirection = ray.direction;

  return intersection;
}

Intersection FindClosestIntersection(const Ray inRay) {
  Intersection closestIntersection = NULL_INTERSECTION;
  for (uint i = 0; i < spheres.length(); i++) {
    Intersection currentIntersection = Intersects(inRay, spheres[i]);
    if (currentIntersection.t < closestIntersection.t)
      closestIntersection = currentIntersection;
  }

  return closestIntersection;
}

// Iterative Approach Of A Recursive Problem (because of glsl)
vec4 TracePath(Ray ray) {
  // Fetch All Intersections
  Intersection intersections[MAX_ITERATIONS];
  for (uint i = 0; i < MAX_ITERATIONS; i++) { intersections[i].t = FLT_MAX; }

  uint intersectionCount = 0;
  for (uint bounceIndex = 0; bounceIndex < MAX_ITERATIONS; bounceIndex++) {
    Intersection intersection = FindClosestIntersection(ray);

    if (intersection.t == FLT_MAX)
      break;

    intersections[bounceIndex] = intersection;

    // Generate the new ray
    ray.origin    = intersection.location + intersection.normal * EPSILON;
    ray.direction = RandomVec3InUnitSphere();
  
    intersectionCount++;
  }

  // Compute Color
  vec4 prevIterationColor = vec4(0.f, 0.f, 0.f, 0.f);
  for (int i = int(intersectionCount) - 1; i >= 0; i--) {
    if (intersections[i].t == FLT_MAX) {
      const float ratio  = gl_GlobalInvocationID.y/float(HEIGHT);
      prevIterationColor = vec4((ratio * skyLightBlue + (1 - ratio) * skyDarkBlue).xyz, 1.f);
    } else {
      const float dotProduct = max(min(dot(intersections[i].inDirection*(-1), intersections[i].normal), 1.f), 0.f);
      prevIterationColor = vec4(float(intersectionCount) / MAX_ITERATIONS);//intersections[i].material.diffuseColor;
    }
  }

  vec4 finalColor = prevIterationColor;

  return finalColor;
}

Ray GenerateCameraRay() {
  const uint pixelX = gl_GlobalInvocationID.x; 
  const uint pixelY = gl_GlobalInvocationID.y; 

  Ray ray;
  ray.origin    = vec3(0.f, 0.f, 0.f);
  ray.direction = normalize(vec3(
    (2.0f *  ((pixelX + RandomFloat()) / float(WIDTH))  - 1.0f) * cameraProjectW,
    (-2.0f * ((pixelY + RandomFloat()) / float(HEIGHT)) + 1.0f) * cameraProjectH,
    1.0f));

  return ray;
}

void main() {
  if(gl_GlobalInvocationID.x >= WIDTH || gl_GlobalInvocationID.y >= HEIGHT)
    return;
  
  vec4 finalPixelColor  = vec4(0.f, 0.f, 0.f, 0.f);
  for (uint sampleIndex = 0; sampleIndex < SPP_COUNT; sampleIndex++)
    finalPixelColor += TracePath(GenerateCameraRay());
  
  finalPixelColor /= SPP_COUNT;

  pixelBuffer[WIDTH * gl_GlobalInvocationID.y + gl_GlobalInvocationID.x] = finalPixelColor;
}