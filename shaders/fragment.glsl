#version 330 core
out vec4 FragColor;

in vec3 PartColor;
in vec3 Normal;
in vec3 FragPos;
in vec4 FragPosLightSpace;
in float Alpha;
in vec3 LocalPos;
in vec3 LocalNormal;

uniform vec3 viewPos;
uniform float reflectance;
uniform samplerCube skybox;
uniform sampler2D shadowMap;
uniform sampler2D faceTexture; // Face texture for camera parts
uniform float hasTexture; // Whether to use face texture
uniform vec3 lightPos; // Make uniform
uniform vec3 partSize; // Added size for scaling studs
uniform float isCamera;  // Flag for camera part visualization

float ShadowCalculation(vec4 fragPosLightSpace)
{
    // perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    // transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;
    // get closest depth value from light's perspective (using [0,1] range fragPosLight as coords)
    float closestDepth = texture(shadowMap, projCoords.xy).r; 
    // get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;
    // check whether current frag pos is in shadow
    vec3 normal = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float bias = max(0.05 * (1.0 - dot(normal, lightDir)), 0.005);
    
    // PCF
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r; 
            shadow += currentDepth - bias > pcfDepth  ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;
    
    // keep the shadow at 0.0 when outside the far_plane region of the light's frustum.
    if(projCoords.z > 1.0)
        shadow = 0.0;
        
    return shadow;
}

void main() {
    // Simple Lighting
    vec3 lightColor = vec3(1.0, 1.0, 0.9);
    
    // Ambient
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * lightColor;
    
    // Diffuse
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    
    // Specular (Plastic look)
    float specularStrength = 0.5;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);  
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = specularStrength * spec * lightColor;  
    
    // Shadow
    float shadow = ShadowCalculation(FragPosLightSpace);
    vec3 lighting = (ambient + (1.0 - shadow) * (diffuse + specular)) * PartColor;
    
    // Reflection map
    vec3 I = normalize(FragPos - viewPos);
    vec3 R = reflect(I, normalize(Normal));
    vec3 reflectionColor = texture(skybox, R).rgb;
    
    // Mix lighting with reflection based on reflectance property
    vec3 finalColor = mix(lighting, reflectionColor, reflectance);
    
    // --- Camera Part / Head Part Face Visualization ---
    if (isCamera > 0.5) {
        // Show face on +Z face (front face in local space)
        // The head rotates, so +Z should be the front
        if (LocalNormal.z > 0.9) {
            if (hasTexture > 0.5) {
                // For +Z face, use x and y coordinates directly
                vec2 uv = LocalPos.xy + vec2(0.5);
                uv.y = 1.0 - uv.y; // Flip Y for correct orientation
                uv = clamp(uv, vec2(0.0), vec2(1.0));
                
                // Sample texture
                vec4 texColor = texture(faceTexture, uv);
                
                // Only apply texture where it has content (alpha > threshold)
                // This prevents making the head transparent where texture is empty
                if (texColor.a > 0.1) {
                    // Blend texture over base color using alpha
                    finalColor = mix(finalColor, texColor.rgb, texColor.a);
                }
                // If texture is transparent, keep the base color (don't make it transparent)
            }
        }
    }
    // --- Procedural Studs (Only if not Camera or on other faces) ---
    else {
        // Use LocalNormal for robust detection of Top/Bottom regardless of rotation
        // Top: LocalNormal.y > 0.5 (Covers Cube Top, Wedge Slope, Sphere Top)
        // Bottom: LocalNormal.y < -0.9 (Covers Cube Bottom, Wedge Bottom, Sphere Bottom Cap)
        
        vec3 studPos = LocalPos * partSize; 
        
        // Top Surface (Studs)
        if (LocalNormal.y > 0.5) {
            // Studs on XZ plane
            vec2 uv = studPos.xz;
            vec2 grid = fract(uv) - 0.5;
            float dist = length(grid);
            // Draw circle (stud)
            if (dist < 0.25) { // Radius 0.25
                // Shading for stud
                // Simple gradient
                finalColor *= 1.2; // Lighter top
            } else if (dist < 0.28) {
                // Outline/Shadow
                finalColor *= 0.8;
            }
        }
        // Bottom Surface (Inlets)
        else if (LocalNormal.y < -0.9) {
            // Inlets on XZ plane
            vec2 uv = studPos.xz;
            vec2 grid = fract(uv) - 0.5;
            float dist = length(grid);
            if (dist < 0.25) { 
                // Darker inside (inlet)
                finalColor *= 0.6; 
            } else if (dist < 0.28) {
                // Highlight rim
                finalColor *= 1.1;
            }
        }
    }
    
    FragColor = vec4(finalColor, Alpha);
}