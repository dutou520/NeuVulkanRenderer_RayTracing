// Nishita 1993 Sky Model
// Based on "Display of The Earth Taking into Account Atmospheric Scattering" (Tomoyuki Nishita et al., 1993)

const float EARTH_RADIUS = 6360000.0;       // 6360 km
const float ATMOSPHERE_RADIUS = 6460000.0;  // 6460 km (100km thickness)
const float RAYLEIGH_SCALE_HEIGHT = 8000.0; // 8 km
const float MIE_SCALE_HEIGHT = 1200.0;      // 1.2 km

// Scattering coefficients (1/m)
const vec3 BETA_RAYLEIGH_BASE = vec3(5.802e-6, 13.558e-6, 33.1e-6);
const vec3 BETA_MIE_SCATTER_BASE = vec3(3.996e-6);
const vec3 BETA_MIE_EXTINCTION_BASE = vec3(4.44e-6);

const float MIE_G = 0.76;
const float SKY_PI = 3.141592653589793;

bool RayAtmosphereIntersect(vec3 orig, vec3 dir, float rad, out float t0, out float t1) {
    float b = dot(orig, dir);
    float c = dot(orig, orig) - rad * rad;
    float d = b * b - c;
    if (d < 0.0) return false;
    float sq = sqrt(d);
    t0 = -b - sq;
    t1 = -b + sq;
    return true;
}

// Rayleigh phase function: P_R(theta) = 3 / (16 * PI) * (1 + cos^2(theta))
float RayleighPhase(float cosTheta) {
    return (3.0 / (16.0 * SKY_PI)) * (1.0 + cosTheta * cosTheta);
}

// Mie Henyey-Greenstein phase function
float MiePhase(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 / (4.0 * SKY_PI)) * (1.0 - g2) / pow(max(denom, 0.0001), 1.5);
}

vec3 GetSunTransmittance(vec3 sunDir, float rayleighScale, float mieTurbidity) {
    float sunY = max(sunDir.y, 0.02);
    float hr = RAYLEIGH_SCALE_HEIGHT / sunY;
    float hm = MIE_SCALE_HEIGHT / sunY;
    vec3 betaRay = BETA_RAYLEIGH_BASE * max(0.01, rayleighScale);
    vec3 betaMie = BETA_MIE_EXTINCTION_BASE * max(0.0, mieTurbidity);
    vec3 tau = betaRay * hr + betaMie * hm;
    return exp(-tau);
}

vec3 EvaluateNishitaSky(vec3 rayDir, vec3 sunDir, float sunIntensity, float rayleighScale, float mieTurbidity, float sunAngularSize, vec3 groundAlbedo, float sunEnabled) {
    // Observer at ground level (10 meters above Earth surface)
    vec3 orig = vec3(0.0, EARTH_RADIUS + 10.0, 0.0);

    float t0, t1;
    if (!RayAtmosphereIntersect(orig, rayDir, ATMOSPHERE_RADIUS, t0, t1)) {
        return vec3(0.0);
    }

    float tMin = max(0.0, t0);
    float tMax = t1;

    // Check if ray hits Earth ground (forward intersection)
    float tg0, tg1;
    bool hitGround = RayAtmosphereIntersect(orig, rayDir, EARTH_RADIUS, tg0, tg1) && tg0 > 0.0;
    if (hitGround) {
        tMax = min(tMax, tg0);
    }

    vec3 betaRayleigh = BETA_RAYLEIGH_BASE * max(0.01, rayleighScale);
    vec3 betaMieScatter = BETA_MIE_SCATTER_BASE * max(0.0, mieTurbidity);
    vec3 betaMieExtinction = BETA_MIE_EXTINCTION_BASE * max(0.0, mieTurbidity);

    const int PRIMARY_STEPS = 10;
    const int LIGHT_STEPS = 4;

    float stepSize = (tMax - tMin) / float(PRIMARY_STEPS);
    float curT = tMin + stepSize * 0.5;

    vec3 sumRayleigh = vec3(0.0);
    vec3 sumMie = vec3(0.0);

    float optDepthR = 0.0;
    float optDepthM = 0.0;

    for (int i = 0; i < PRIMARY_STEPS; ++i) {
        vec3 pos = orig + rayDir * curT;
        float height = length(pos) - EARTH_RADIUS;

        if (height > 0.0) {
            float hr = exp(-height / RAYLEIGH_SCALE_HEIGHT) * stepSize;
            float hm = exp(-height / MIE_SCALE_HEIGHT) * stepSize;

            optDepthR += hr;
            optDepthM += hm;

            // Light optical depth towards sun
            float lt0, lt1;
            RayAtmosphereIntersect(pos, sunDir, ATMOSPHERE_RADIUS, lt0, lt1);
            float lightStepSize = lt1 / float(LIGHT_STEPS);
            float lightCurT = lightStepSize * 0.5;

            float lightOptDepthR = 0.0;
            float lightOptDepthM = 0.0;
            bool sunBlocked = false;

            // Check ground shadow on light ray
            float ltg0, ltg1;
            if (RayAtmosphereIntersect(pos, sunDir, EARTH_RADIUS, ltg0, ltg1) && ltg0 > 0.0) {
                sunBlocked = true;
            }

            if (!sunBlocked) {
                for (int j = 0; j < LIGHT_STEPS; ++j) {
                    vec3 lpos = pos + sunDir * lightCurT;
                    float lheight = length(lpos) - EARTH_RADIUS;
                    if (lheight > 0.0) {
                        lightOptDepthR += exp(-lheight / RAYLEIGH_SCALE_HEIGHT) * lightStepSize;
                        lightOptDepthM += exp(-lheight / MIE_SCALE_HEIGHT) * lightStepSize;
                    }
                    lightCurT += lightStepSize;
                }

                vec3 tau = betaRayleigh * (optDepthR + lightOptDepthR) + betaMieExtinction * (optDepthM + lightOptDepthM);
                vec3 attenuation = exp(-tau);

                sumRayleigh += hr * attenuation;
                sumMie += hm * attenuation;
            }
        }
        curT += stepSize;
    }

    float cosTheta = dot(rayDir, sunDir);
    float pR = RayleighPhase(cosTheta);
    float pM = MiePhase(cosTheta, MIE_G);

    vec3 skyRadiance = sunIntensity * (sumRayleigh * betaRayleigh * pR + sumMie * betaMieScatter * pM);

    // Sun Disk (only when sunEnabled > 0.5)
    if (sunEnabled > 0.5 && !hitGround && cosTheta > 0.0) {
        float sunAngle = max(0.001, sunAngularSize);
        float minCosSun = cos(sunAngle);
        if (cosTheta > minCosSun) {
            // Direct sun disk attenuated by total view atmosphere
            vec3 sunTau = betaRayleigh * optDepthR + betaMieExtinction * optDepthM;
            vec3 sunAtten = exp(-sunTau);
            float diskIntensity = smoothstep(minCosSun, minCosSun + (1.0 - minCosSun) * 0.1, cosTheta);
            skyRadiance += sunAtten * (sunIntensity * 60.0) * diskIntensity;
        }
    }

    // Ground illumination
    if (hitGround) {
        skyRadiance = groundAlbedo * max(0.0, sunDir.y) * sunIntensity * 0.05;
    }

    return max(vec3(0.0), skyRadiance);
}

vec3 EvaluateNishitaSky(vec3 rayDir, vec3 sunDir, float sunIntensity, float rayleighScale, float mieTurbidity, float sunAngularSize, vec3 groundAlbedo) {
    return EvaluateNishitaSky(rayDir, sunDir, sunIntensity, rayleighScale, mieTurbidity, sunAngularSize, groundAlbedo, 1.0);
}
