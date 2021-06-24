#include "common.hlsli"

struct VertexPN {
    float3 Position;
    float3 Normal;
};

[shader("closesthit")]
void mainLightCHS(inout Payload payload, MyAttribute attrib) {
    if (checkRecursiveLimit(payload)) {
        return;
    }
    payload.color = float3(1, 1, 1);
}

