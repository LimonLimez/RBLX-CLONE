#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal; // Add normal attribute

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;
uniform vec3 color;
uniform float transparency;

out vec3 PartColor;
out vec3 Normal;
out vec3 FragPos;
out vec4 FragPosLightSpace;
out float Alpha;
out vec3 LocalPos; // Pass local position to fragment shader
out vec3 LocalNormal; // Pass local normal for stud detection

void main() {
    LocalPos = aPos; // Store local position before model transform
    LocalNormal = aNormal;
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;    
    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);
    gl_Position = projection * view * vec4(FragPos, 1.0);
    PartColor = color;
    Alpha = 1.0 - transparency;
}
