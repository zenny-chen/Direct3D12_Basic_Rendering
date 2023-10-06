/** Model view translation matrix *
 * [ 1  0  0  0
     0  1  0  0
     0  0  1  0
     x  y  z  1
 * ]
*/

/** Ortho projection matrix *
 * [ 2/(r-l)       0             0             0
     0             2/(t-b)       0             0
     0             0             -2/(f-n)      0
     -(r+l)/(r-l)  -(t+b)/(t-b)  -(f+n)/(f-n)  1
 * ]
*/

/** rotate matrix *
 * [x^2*(1-c)+c  xy*(1-c)+zs  xz(1-c)-ys  0
    xy(1-c)-zs   y^2*(1-c)+c  yz(1-c)+xs  0
    xz(1-c)+ys   yz(1-c)-xs   z^2(1-c)+c  0
    0            0            0           1
 * ]
 * |(x, y, z)| must be 1.0
*/

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

cbuffer cbRotationAngle : register(b0)
{
    float rotAngle;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
{
    // glTranslate(offset, offset, -2.3, 1.0)
    const float4x4 translateMatrix = {
        1.0f, 0.0f, 0.0f, 0.0f,     // row 0
        0.0f, 1.0f, 0.0f, 0.0f,     // row 1
        0.0f, 0.0f, 1.0f, 0.0f,     // row 2
        0.0f, 0.0f, -2.3f, 1.0f     // row 3
    };

    const float rotRadian = radians(rotAngle);

    // glRotate(u_angle, 0.0, 0.0, 1.0)
    const float4x4 rotateMatrix = {
        cos(rotRadian), sin(rotRadian), 0.0f, 0.0f,     // row 0
        -sin(rotRadian), cos(rotRadian), 0.0f, 0.0f,    // row 1
        0.0f, 0.0f, 1.0f, 0.0f,                         // row 2
        0.0f, 0.0f, 0.0f, 1.0f                          // row 3
    };

    // glOrtho(-1.0, 1.0, -1.0, 1.0, 1.0, 3.0)
    const float4x4 projectionMatrix = {
        1.0f, 0.0f, 0.0f, 0.0f,     // row 0
        0.0f, 1.0f, 0.0f, 0.0f,     // row 1
        0.0f, 0.0f, -1.0f, 0.0f,    // row 2
        0.0f, 0.0f, -2.0f, 1.0f     // row 3
    };

    PSInput result;
    result.position = mul(position, mul(rotateMatrix, mul(translateMatrix, projectionMatrix)));
    result.color = color;

    return result;
}

