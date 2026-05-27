struct VSInput {
    float4 position : POSITION0;
    float4 attr1 : ATTRIBUTE1;
};

struct VSOutput {
    float4 position : SV_Position;
    uint4 id : TEXCOORD0;
};

VSOutput VSMain(VSInput input, uint vertex_id : SV_VertexID) {
    VSOutput output;
    output.position = input.position + float4(input.attr1.xyz * 0.001f, 0.0f);
    output.id = uint4(vertex_id, vertex_id + 1, vertex_id + 2, 255);
    return output;
}

uint4 PSMain(VSOutput input) : SV_Target0 {
    return input.id;
}
