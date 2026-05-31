#version 310 es
precision highp float;
precision highp int;

// =============================================================================
//  water.frag — блики, Fresnel-отражение и солнечные искры на воде
// =============================================================================
//  Расчёт освещения поверхности воды:
//   1. Fresnel  — рост отражательной способности под скользящим углом (Schlick).
//   2. Specular — резкий солнечный блик (Blinn-Phong, высокая степень).
//   3. Glitter  — анимированные высокочастотные искры на гребнях волн.
//   4. Sky tint — подмешивание цвета неба по Fresnel (имитация отражения).
//  Альфа берётся из u_color.a — это сохраняет вашу логику прозрачности
//  (над/под водой + пульсация из GameScene::update), плюс лёгкий буст по бликам.
// =============================================================================

layout(location = TEXCOORD0) in vec2 v_texCoord;
layout(location = TEXCOORD1) in vec3 v_normal;
layout(location = TEXCOORD2) in vec3 v_worldPos;
layout(location = TEXCOORD3) in float v_time;   // время приходит из VS (единственный u_time)

layout(binding = 0) uniform sampler2D u_tex0;  // атлас террейна (тайл воды)

layout(std140) uniform fs_ub {
    vec4  u_color;     // авто-биндинг: цвет/opacity нода (Pass _locColor)
    vec3  u_camWorld;  // мировая позиция камеры (обновляется каждый кадр)
    vec3  u_lightDir;  // направление лучей солнца (ОТ солнца К сцене), нормировано
    // u_time НЕ объявляем здесь: он живёт только в vs_ub и приходит как varying v_time
    // (см. water.vert) — иначе дублирование имени ломает вершинную анимацию волн.
};

layout(location = SV_Target0) out vec4 FragColor;

// Палитра воды (можно вынести в uniform при желании)
const vec3 DEEP_COLOR    = vec3(0.04, 0.22, 0.42);  // глубокая вода
const vec3 SHALLOW_COLOR = vec3(0.10, 0.45, 0.62);  // мелководье / гребни
const vec3 SKY_COLOR     = vec3(0.55, 0.78, 0.95);  // отражение неба
const vec3 SUN_COLOR     = vec3(1.00, 0.97, 0.85);  // солнечный блик

// Дешёвый псевдослучайный шум для искр
float hash(vec2 p)
{
    p = fract(p * vec2(123.34, 345.45));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

void main(void)
{
    vec3 N = normalize(v_normal);
    vec3 V = normalize(u_camWorld - v_worldPos);          // к камере
    vec3 L = normalize(-u_lightDir);                      // к солнцу
    vec3 H = normalize(L + V);                            // полу-вектор

    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);

    // --- 1. Fresnel (Schlick), F0 ~0.02 для воды ---
    float fresnel = 0.02 + 0.98 * pow(1.0 - NdotV, 5.0);

    // --- Базовый цвет: глубина имитируется по Fresnel + текстура атласа ---
    vec3  tex      = texture(u_tex0, v_texCoord).rgb;
    vec3  baseCol  = mix(DEEP_COLOR, SHALLOW_COLOR, NdotV);
    baseCol        = mix(baseCol, baseCol * (0.7 + 0.6 * tex.b), 0.35);

    // --- 4. Отражение неба по Fresnel ---
    vec3 waterCol = mix(baseCol, SKY_COLOR, fresnel * 0.65);

    // --- 2. Солнечный блик (Blinn-Phong, резкий) ---
    float specular = pow(NdotH, 220.0) * 1.4;

    // --- 3. Анимированные искры на бликовой зоне ---
    // Положение искр фиксировано по ячейкам, а яркость ПЛАВНО пульсирует во времени.
    // Раньше floor(u_time*1.7) перерисовывал весь узор скачками → блики «дёргались».
    vec2  gp      = v_worldPos.xz * 7.0;
    vec2  cell    = floor(gp);
    float rnd     = hash(cell);
    float twinkle = 0.5 + 0.5 * sin(v_time * 3.0 + rnd * 6.2831853);  // гладкая пульсация 0..1
    float sparkle = pow(rnd, 40.0) * pow(twinkle, 4.0);              // редкие яркие точки
    float glint   = sparkle * smoothstep(0.55, 0.95, NdotH) * 2.0;

    vec3 color = waterCol + SUN_COLOR * (specular + glint);

    // Альфа: из нода + усиление на бликах/гранях (вода у кромки плотнее)
    float alpha = clamp(u_color.a + fresnel * 0.18 + specular * 0.5, 0.0, 1.0);

    FragColor = vec4(color, alpha);
}
