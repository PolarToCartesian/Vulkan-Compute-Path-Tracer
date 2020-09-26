#version 440

// User defined #defines
#define WORKGROUP_SIZE (32)       // The Number Of Threads Per Workgroup (NVIDIA: 32, AMD: 64)
#define WIDTH          (1920 / 2) // The Target Surface's Width  In Pixels
#define HEIGHT         (1080 / 2) // The Target Surface's Height In Pixels
#define SPP_COUNT      (100)      // Number Of Ray Samples Per Pixel
#define MAX_ITERATIONS (10)       // The Maximum Number Of Iterations For Each Sample

// Raw constant #defines
#define FLT_MAX (3.402823466e+38) // Highest 32-bit Floating Point Number Possible
#define EPSILON (0.001f)          // A Very Small Value
#define M_PI    (3.1415926535897) // π

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
const float[1000] randomNumbers = { 0.199597f, 0.604987f, 0.255558f, 0.421514f, 0.720092f, 0.815522f, 0.192279f, 0.385067f, 0.350586f, 0.397595f, 0.357564f, 0.748578f, 0.00414681f, 0.533777f, 0.995393f, 0.907929f, 0.494525f, 0.472084f, 0.864498f, 0.695326f, 0.938409f, 0.785484f, 0.290453f, 0.13312f, 0.943201f, 0.926033f, 0.320409f, 0.0662487f, 0.25414f, 0.421945f, 0.667499f, 0.444524f, 0.838885f, 0.908202f, 0.8063f, 0.291879f, 0.114376f, 0.875398f, 0.247916f, 0.045868f, 0.535327f, 0.491882f, 0.642606f, 0.184197f, 0.154249f, 0.14628f, 0.939923f, 0.979867f, 0.503506f, 0.478285f, 0.491597f, 0.0545161f, 0.847528f, 0.0108021f, 0.934526f, 0.282655f, 0.0207591f, 0.329495f, 0.328761f, 0.560112f, 0.119835f, 0.296947f, 0.289384f, 0.83466f, 0.164883f, 0.0987901f, 0.0792031f, 0.258547f, 0.0754077f, 0.0143626f, 0.318207f, 0.483693f, 0.0715536f, 0.998425f, 0.322974f, 0.879418f, 0.261024f, 0.49866f, 0.453179f, 0.347203f, 0.638452f, 0.274543f, 0.595394f, 0.640481f, 0.798533f, 0.680735f, 0.95186f, 0.4518f, 0.969803f, 0.419822f, 0.00485671f, 0.727772f, 0.475605f, 0.816288f, 0.55194f, 0.550753f, 0.601672f, 0.908048f, 0.35448f, 0.863961f, 0.199465f, 0.261941f, 0.705218f, 0.170833f, 0.163966f, 0.963891f, 0.885989f, 0.42689f, 0.91419f, 0.286472f, 0.398857f, 0.660428f, 0.658615f, 0.197595f, 0.117005f, 0.162202f, 0.606252f, 0.463889f, 0.137883f, 0.98649f, 0.217564f, 0.462859f, 0.642046f, 0.457335f, 0.132928f, 0.0262717f, 0.581032f, 0.53622f, 0.255176f, 0.252742f, 0.79537f, 0.684744f, 0.839087f, 0.716997f, 0.779449f, 0.274913f, 0.649963f, 0.0966604f, 0.631428f, 0.848244f, 0.293896f, 0.296141f, 0.561216f, 0.114576f, 0.566175f, 0.180761f, 0.77445f, 0.75926f, 0.014139f, 0.590507f, 0.175839f, 0.782789f, 0.603193f, 0.117811f, 0.429538f, 0.995071f, 0.836666f, 0.598363f, 0.119665f, 0.210369f, 0.0763501f, 0.039572f, 0.256635f, 0.464337f, 0.27509f, 0.0565498f, 0.920264f, 0.276731f, 0.714882f, 0.754172f, 0.356692f, 0.867855f, 0.0365478f, 0.591239f, 0.179156f, 0.838147f, 0.192738f, 0.0984936f, 0.676551f, 0.716241f, 0.613769f, 0.398663f, 0.61213f, 0.0574901f, 0.069936f, 0.475339f, 0.556998f, 0.619985f, 0.805505f, 0.753665f, 0.0128323f, 0.232355f, 0.465625f, 0.651037f, 0.893679f, 0.0899103f, 0.140068f, 0.0414808f, 0.163967f, 0.0753036f, 0.90282f, 0.652718f, 0.88993f, 0.0572703f, 0.483928f, 0.672284f, 0.391029f, 0.698424f, 0.235487f, 0.99883f, 0.712831f, 0.636613f, 0.577314f, 0.601191f, 0.773159f, 0.967372f, 0.223231f, 0.139704f, 0.892793f, 0.80469f, 0.996191f, 0.869575f, 0.385519f, 0.247781f, 0.913553f, 0.742359f, 0.335352f, 0.475378f, 0.0570664f, 0.646134f, 0.749158f, 0.502298f, 0.233596f, 0.0816442f, 0.831163f, 0.499205f, 0.363309f, 0.218481f, 0.928966f, 0.365928f, 0.70506f, 0.464998f, 0.487259f, 0.949172f, 0.0130392f, 0.370645f, 0.204907f, 0.726774f, 0.511039f, 0.560986f, 0.694753f, 0.16565f, 0.421159f, 0.802984f, 0.84258f, 0.188738f, 0.273293f, 0.584147f, 0.590206f, 0.297066f, 0.120813f, 0.361537f, 0.536748f, 0.764223f, 0.896729f, 0.382177f, 0.381494f, 0.641732f, 0.924145f, 0.144965f, 0.605816f, 0.31046f, 0.98744f, 0.635756f, 0.420114f, 0.180892f, 0.600533f, 0.86274f, 0.630739f, 0.177256f, 0.948265f, 0.111138f, 0.103353f, 0.124599f, 0.19368f, 0.790318f, 0.603268f, 0.678606f, 0.991479f, 0.168355f, 0.243277f, 0.948614f, 0.891675f, 0.977185f, 0.430504f, 0.0498834f, 0.717779f, 0.700717f, 0.913892f, 0.844724f, 0.0909745f, 0.903162f, 0.73582f, 0.288858f, 0.892223f, 0.134986f, 0.802553f, 0.97931f, 0.887141f, 0.22055f, 0.114842f, 0.692509f, 0.427549f, 0.645941f, 0.214635f, 0.449013f, 0.129613f, 0.00375897f, 0.930036f, 0.592732f, 0.742017f, 0.953853f, 0.807412f, 0.234893f, 0.893565f, 0.00829244f, 0.725876f, 0.833217f, 0.0901207f, 0.490959f, 0.969823f, 0.610366f, 0.28824f, 0.964868f, 0.895023f, 0.0366296f, 0.281412f, 0.173797f, 0.923646f, 0.783377f, 0.864133f, 0.817807f, 0.492471f, 0.739527f, 0.130182f, 0.891254f, 0.166551f, 0.19944f, 0.111839f, 0.665255f, 0.721909f, 0.639995f, 0.688461f, 0.0960512f, 0.222483f, 0.429319f, 0.671098f, 0.672718f, 0.89815f, 0.750067f, 0.642731f, 0.128336f, 0.142941f, 0.256261f, 0.492085f, 0.257735f, 0.619495f, 0.213408f, 0.699803f, 0.920064f, 0.184981f, 0.00890118f, 0.59651f, 0.0157422f, 0.0938643f, 0.519085f, 0.407131f, 0.541971f, 0.967385f, 0.79704f, 0.711936f, 0.661744f, 0.944692f, 0.635481f, 0.180704f, 0.566781f, 0.904928f, 0.221765f, 0.257301f, 0.984379f, 0.00722271f, 0.0838547f, 0.143073f, 0.964426f, 0.416246f, 0.562115f, 0.81624f, 0.455707f, 0.0990105f, 0.401521f, 0.646733f, 0.6454f, 0.540078f, 0.045286f, 0.100766f, 0.144638f, 0.533128f, 0.518436f, 0.854319f, 0.782753f, 0.236515f, 0.394842f, 0.56482f, 0.805875f, 0.941737f, 0.943546f, 0.175699f, 0.136224f, 0.452205f, 0.50925f, 0.203362f, 0.653147f, 0.890194f, 0.412692f, 0.646633f, 0.0111724f, 0.532499f, 0.931998f, 0.260469f, 0.76115f, 0.941364f, 0.786769f, 0.285829f, 0.825026f, 0.0966909f, 0.287099f, 0.99207f, 0.0365438f, 0.107924f, 0.618964f, 0.636868f, 0.629892f, 0.0322444f, 0.266686f, 0.295857f, 0.491013f, 0.728626f, 0.34647f, 0.192963f, 0.75249f, 0.505772f, 0.589707f, 0.975929f, 0.372293f, 0.0676739f, 0.830597f, 0.613582f, 0.873908f, 0.0223793f, 0.93854f, 0.168431f, 0.551551f, 0.703429f, 0.911513f, 0.0695175f, 0.186045f, 0.699685f, 0.0653194f, 0.394191f, 0.755561f, 0.424964f, 0.1625f, 0.050553f, 0.688279f, 0.177003f, 0.383356f, 0.0563712f, 0.238585f, 0.832928f, 0.664004f, 0.818007f, 0.469923f, 0.617989f, 0.193306f, 0.968948f, 0.470138f, 0.538931f, 0.432459f, 0.665139f, 0.650698f, 0.747556f, 0.183671f, 0.968961f, 0.563727f, 0.580167f, 0.700428f, 0.0738738f, 0.911959f, 0.705592f, 0.69623f, 0.0580394f, 0.0645472f, 0.799005f, 0.174736f, 0.613838f, 0.306287f, 0.855581f, 0.165321f, 0.91612f, 0.630336f, 0.956779f, 0.890554f, 0.501114f, 0.714703f, 0.907779f, 0.877341f, 0.5773f, 0.55107f, 0.438894f, 0.832637f, 0.543427f, 0.414126f, 0.632788f, 0.231363f, 0.308495f, 0.0412881f, 0.192463f, 0.284209f, 0.730449f, 0.900284f, 0.405939f, 0.596234f, 0.719513f, 0.347826f, 0.869204f, 0.207232f, 0.323294f, 0.819452f, 0.200805f, 0.493272f, 0.908608f, 0.766762f, 0.462495f, 0.798554f, 0.0459776f, 0.827266f, 0.476462f, 0.423438f, 0.566897f, 0.350476f, 0.955634f, 0.848733f, 0.182972f, 0.223292f, 0.711187f, 0.893077f, 0.251123f, 0.724772f, 0.710811f, 0.898751f, 0.218258f, 0.200893f, 0.14561f,  0.882883f, 0.395595f, 0.144703f, 0.794973f, 0.622345f, 0.661621f, 0.357816f, 0.566107f, 0.996509f, 0.906827f, 0.526943f, 0.240701f, 0.37052f, 0.15171f, 0.145867f, 0.195744f, 0.00592089f, 0.266015f, 0.942568f, 0.147965f, 0.0350275f, 0.41055f, 0.186301f, 0.639831f, 0.786075f, 0.900185f, 0.16039f, 0.730898f, 0.592034f, 0.0984173f, 0.172115f, 0.770558f, 0.889407f, 0.32866f, 0.0975621f, 0.392982f, 0.282305f, 0.517621f, 0.776283f, 0.647509f, 0.700204f, 0.867562f, 0.680769f, 0.391504f, 0.481068f, 0.0146101f, 0.787989f, 0.835125f, 0.817836f, 0.501559f, 0.64041f, 0.0362685f, 0.698073f, 0.387414f, 0.711341f, 0.0328069f, 0.578711f, 0.276045f, 0.502735f, 0.532282f, 0.153564f, 0.640893f, 0.324248f, 0.611137f, 0.639803f, 0.0535753f, 0.00723684f, 0.195116f, 0.647639f, 0.171853f, 0.613546f, 0.76662f, 0.105029f, 0.451737f, 0.706308f, 0.587662f, 0.530912f, 0.797519f, 0.704572f, 0.997244f, 0.521352f, 0.616776f, 0.140664f, 0.226086f, 0.390515f, 0.143378f, 0.594788f, 0.192412f, 0.952663f, 0.303675f, 0.628169f, 0.514937f, 0.119891f, 0.0142412f, 0.725019f, 0.161026f, 0.0623053f, 0.771054f, 0.581882f, 0.987366f, 0.387268f, 0.968645f, 0.0368345f, 0.681872f, 0.8013f, 0.636749f, 0.872977f, 0.584643f, 0.468048f, 0.809518f, 0.606463f, 0.431625f, 0.744441f, 0.867831f, 0.195912f, 0.290907f, 0.982034f, 0.569141f, 0.416703f, 0.000553131f, 0.232654f, 0.643564f, 0.792894f, 0.0906864f, 0.823185f, 0.44229f, 0.870749f, 0.094511f, 0.0765932f, 0.9337f, 0.813633f, 0.855686f, 0.480252f, 0.81063f, 0.671934f, 0.107144f, 0.452391f, 0.0102196f, 0.978916f, 0.498415f, 0.00111079f, 0.227843f, 0.814619f, 0.483144f, 0.284697f, 0.163718f, 0.637544f, 0.893997f, 0.444231f, 0.0108444f, 0.511904f, 0.0207537f, 0.868698f, 0.201767f, 0.135786f, 0.641902f, 0.63698f, 0.00485027f, 0.188194f, 0.342811f, 0.904838f, 0.567314f, 0.522027f, 0.660178f, 0.158126f, 0.0785167f, 0.789012f, 0.630418f, 0.130244f, 0.49751f, 0.528462f, 0.399019f, 0.00915778f, 0.620947f, 0.891864f, 0.670242f, 0.462476f, 0.342991f, 0.694961f, 0.946854f, 0.670286f, 0.523117f, 0.128547f, 0.1131f, 0.985722f, 0.520914f, 0.750023f, 0.683531f, 0.323803f, 0.960438f, 0.256519f, 0.0123692f, 0.86433f, 0.703497f, 0.354623f, 0.412379f, 0.679627f, 0.4803f, 0.0851257f, 0.0552458f, 0.200105f, 0.585031f, 0.496093f, 0.709309f, 0.561322f, 0.0316569f, 0.300427f, 0.532929f, 0.39548f, 0.720142f, 0.886776f, 0.767199f, 0.0641108f, 0.366764f, 0.836993f, 0.216178f, 0.998169f, 0.987225f, 0.0619581f, 0.306346f, 0.362375f, 0.433309f, 0.382029f, 0.823654f, 0.202664f, 0.519799f, 0.67241f, 0.651936f, 0.538402f, 0.881851f, 0.436804f, 0.408586f, 0.891425f, 0.921423f, 0.0845329f, 0.568851f, 0.565772f, 0.101898f, 0.968398f, 0.721404f, 0.465441f, 0.256595f, 0.532107f, 0.874373f, 0.919419f, 0.438091f, 0.850347f, 0.783384f, 0.234676f, 0.695155f, 0.92867f, 0.762211f, 0.258354f, 0.438337f, 0.789661f, 0.393908f, 0.400944f, 0.392954f, 0.547316f, 0.999653f, 0.0157389f, 0.819053f, 0.280515f, 0.603952f, 0.303337f, 0.834401f, 0.555301f, 0.722543f, 0.464627f, 0.0404077f, 0.316715f, 0.509391f, 0.0770068f, 0.65071f, 0.165766f, 0.091059f, 0.0842007f, 0.0436845f, 0.508642f, 0.876002f, 0.942838f, 0.0361741f, 0.303108f, 0.315701f, 0.915692f, 0.0220238f, 0.782612f, 0.950066f, 0.429853f, 0.480035f, 0.243326f, 0.78103f, 0.4773f, 0.688337f, 0.476257f, 0.794015f, 0.270835f, 0.917234f, 0.588906f, 0.337772f, 0.58123f, 0.737005f, 0.887304f, 0.215648f, 0.28394f, 0.916682f, 0.262871f, 0.854793f, 0.522426f, 0.726113f, 0.103561f, 0.409966f, 0.346411f, 0.948339f, 0.548415f, 0.439915f, 0.642f, 0.485675f, 0.860085f, 0.846461f, 0.397005f, 0.671292f, 0.460856f, 0.396141f, 0.0653073f, 0.970247f, 0.63457f, 0.539358f, 0.6996f, 0.912391f, 0.769901f, 0.37766f, 0.525321f, 0.941206f, 0.564657f, 0.525525f, 0.542999f, 0.613727f, 0.718994f, 0.979324f, 0.128216f, 0.949264f, 0.325878f, 0.735954f, 0.980225f, 0.269587f, 0.996746f, 0.158121f, 0.510582f, 0.903566f, 0.879182f, 0.0290898f, 0.625907f, 0.426035f, 0.648778f, 0.125296f, 0.354931f, 0.531968f, 0.570994f, 0.702381f, 0.916268f, 0.297439f, 0.619216f, 0.435081f, 0.550982f, 0.197225f, 0.771644f, 0.852344f, 0.865225f, 0.738545f, 0.683739f, 0.287092f, 0.942307f, 0.711667f, 0.0830767f, 0.0645204f, 0.382111f, 0.916265f, 0.813614f, 0.123312f, 0.689993f, 0.794759f, 0.542945f, 0.85528f, 0.929388f, 0.914361f, 0.457666f, 0.647891f, 0.0166076f, 0.917181f, 0.765794f, 0.102935f, 0.339754f, 0.145325f, 0.36664f, 0.517964f, 0.589173f, 0.514363f, 0.959826f, 0.532476f, 0.959562f, 0.498435f, 0.652197f, 0.817202f, 0.501979f, 0.772396f, 0.708271f, 0.419584f, 0.886719f, 0.193582f, 0.833546f, 0.306087f, 0.0203617f, 0.550931f, 0.190094f, 0.865495f, 0.534151f, 0.562979f, 0.510257f, 0.855154f, 0.110657f, 0.280008f, 0.495069f, 0.863048f, 0.264502f, 0.631242f, 0.764268f, 0.51686f, 0.761685f, 0.760911f, 0.155105f, 0.0936222f, 0.990336f, 0.867793f, 0.032875f, 0.505016f, 0.342684f };

float RandomFloat(vec2 xy) {
  // TODO:: prevent overflow of "randomNumbers"
  return randomNumbers[randomNumberIndex++];
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
  float    t;        // distance from the ray's origin to intersection point
  vec3     location; // location of intersection
  vec3     normal;   // the normal at the intersection
  Material material; // the material that the intersected object is made of
};

#define NULL_RAY          (Ray(vec3(0.0f), vec3(0.0f)))
#define NULL_INTERSECTION (Intersection(FLT_MAX, vec3(0.f), vec3(0.f), Material(vec4(0.f), vec4(0.f))))

Sphere[2] spheres = {
  Sphere(vec3(0.00f, 0.f, 2.f), 0.5f, Material(vec4(1.f, 0.f, 0.f, 0.f), vec4(0.f, 0.f, 0.f, 0.f))),
  Sphere(vec3(1.25f, 0.f, 1.f), 0.5f, Material(vec4(0.f, 0.f, 0.f, 0.f), vec4(1.f, 1.f, 1.f, 0.f)))
};

Intersection Intersects(const Ray ray, const Sphere sphere) {
  const vec3  L    = sphere.center - ray.origin;
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
  intersection.t        = t0;
  intersection.location = ray.origin + t0 * ray.direction;
  intersection.normal   = normalize(intersection.location - sphere.center);
  intersection.material = sphere.material;

  return intersection;
}

vec3 RandomVec3InUnitSphere(vec3 seed) {
  return vec3(
    RandomFloat(seed.xy),
    RandomFloat(seed.yz),
    RandomFloat(seed.xz)
  );
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
  vec4 accumulatedColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);

  Intersection previousIntersection = NULL_INTERSECTION;
  float cosTheta = 1.f;
  bool bStopLoop = false;
  uint bounceIndex = 0;
  for (; bounceIndex < MAX_ITERATIONS; bounceIndex++) {
    const Intersection intersection = FindClosestIntersection(ray);

    if (bounceIndex > 0)
      cosTheta = min(max(dot(previousIntersection.normal, ray.direction*(-1)), 0.0f), 1.f);

    vec4 currentBounceColor = vec4(0.0f, 0.0f, 0.0f, 0.0f);
    if (intersection.t != FLT_MAX) {
      currentBounceColor = intersection.material.diffuseColor 
                         + intersection.material.emittance;

      ray.origin    = intersection.location + intersection.normal * EPSILON;
      ray.direction = RandomVec3InUnitSphere(intersection.location);

      //currentBounceColor += TracePath(ray, recursionDepth + 1) * dot(newRay.direction, intersection.normal);
    } else {
      // Draw Sky Color
      const float ratio = gl_GlobalInvocationID.y/float(HEIGHT);
      currentBounceColor += vec4((ratio * skyLightBlue + (1 - ratio) * skyDarkBlue).xyz, 0.f);
      currentBounceColor = vec4(0.f);
      bStopLoop = true;
    }

    if (bounceIndex > 0)
      currentBounceColor *= cosTheta;

    accumulatedColor += currentBounceColor;

    if (bStopLoop)
      break;
    
    previousIntersection = intersection;
  }

  return min(max(accumulatedColor / float(bounceIndex + 1), 0.f), 1.f);
}

Ray MakeCameraRay() {
  const uint x = gl_GlobalInvocationID.x; 
  const uint y = gl_GlobalInvocationID.y; 

  Ray ray;
  ray.origin    = vec3(0.f, 0.f, 0.f);
  ray.direction = 
    normalize(
      vec3(
        (2.0f *  ((x + 1.5f) / float(WIDTH))  - 1.0f) * cameraProjectW,
        (-2.0f * ((y + 1.5f) / float(HEIGHT)) + 1.0f) * cameraProjectH,
        1.0f
      )
    );

  return ray;
}

void main() {
  if(gl_GlobalInvocationID.x >= WIDTH || gl_GlobalInvocationID.y >= HEIGHT)
    return;
  
  vec4 finalPixelColor  = vec4(0.f, 0.f, 0.f, 0.f);
  for (uint sampleIndex = 0; sampleIndex < SPP_COUNT; sampleIndex++) {
    finalPixelColor += TracePath(MakeCameraRay());
  }
  finalPixelColor /= SPP_COUNT;

  pixelBuffer[WIDTH * gl_GlobalInvocationID.y + gl_GlobalInvocationID.x] = finalPixelColor;
}