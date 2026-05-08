#include "VoxelCollisionResolver.h"
#include "ChunkManager.h"
#include <algorithm>
#include <cmath>

namespace voxel_collision
{
    VoxelCollisionResolver::VoxelCollisionResolver(const ChunkManager* chunkMgr)
        : _chunkMgr(chunkMgr) {}

    // =========================================================================
    //  resolve — главный цикл разрешения коллизий
    // =========================================================================
    CollisionResult VoxelCollisionResolver::resolve(float dt, const ax::Vec3& velocity, PlayerCapsule& capsule)
    {
        CollisionResult result;
        result.resolvedPosition = capsule.bottomPos;
        result.isGrounded = false;
        result.collisionNormal = ax::Vec3::ZERO;
        // 🔧 FIX #4: Явная инициализация нового поля
        result.hitCeiling = false;

        if (dt <= 0.0f || !_chunkMgr) return result;

        // Порядок проходов важен: Y первым для корректной гравитации и прыжков

        // 🔧 FIX #4: Сохраняем флаг потолочного столкновения ДО проходов X/Z.
        // Y-проход может установить collisionNormal.y > 0 (потолок),
        // но последующий X или Z проход перезапишет collisionNormal на горизонтальный.
        // Флаг hitCeiling сохраняет информацию о потолке независимо от последующих осей.
        sweepAxis(dt, velocity, capsule, 1, result.isGrounded, result); // Y
        if (result.collisionNormal.y > 0.5f) {
            result.hitCeiling = true;
        }
        sweepAxis(dt, velocity, capsule, 0, result.isGrounded, result); // X
        sweepAxis(dt, velocity, capsule, 2, result.isGrounded, result); // Z

        // 🔧 FIX #3 + #11: Явный ground probe после sweep-проходов.
        //
        // Проблема: при очень высоком FPS (144+, 240+) шаг движения за кадр
        // может быть меньше минимального yOff (0.01) в checkCollisionAt.
        // Y-sweep "проскальзывает" и не детектирует коллизию с полом,
        // флаг isGrounded сбрасывается, и игрок "дребезжит" на земле.
        //
        // Решение: отдельная проверка чуть ниже подошвы (0.1 единицы).
        // Это не влияет на Y-sweep (который по-прежнему обрабатывает столкновения),
        // а лишь корректирует флаг isGrounded для граничного случая.
        if (!result.isGrounded) {
            float r = capsule.radius - 0.02f;
            // Проверяем несколько точек под подошвой с учётом радиуса капсулы
            ax::Vec3 probeCenter = result.resolvedPosition + ax::Vec3(0, -0.1f, 0);
            if (isSolidAt(probeCenter) ||
                isSolidAt(probeCenter + ax::Vec3( r, 0,  r)) ||
                isSolidAt(probeCenter + ax::Vec3(-r, 0,  r)) ||
                isSolidAt(probeCenter + ax::Vec3( r, 0, -r)) ||
                isSolidAt(probeCenter + ax::Vec3(-r, 0, -r)))
            {
                result.isGrounded = true;
            }
        }

        // Обновляем позицию капсулы после разрешения
        capsule.bottomPos = result.resolvedPosition;
        return result;
    }

    bool VoxelCollisionResolver::isSolidAt(const ax::Vec3& worldPos) const
    {
        return _chunkMgr->getBlockAtWorldPos(worldPos) != BLOCK_AIR;
    }

    // =========================================================================
    //  sweepAxis — развёртка вдоль одной оси с SUB-STEPPING
    // =========================================================================
    void VoxelCollisionResolver::sweepAxis(float dt, const ax::Vec3& velocity, PlayerCapsule& capsule, 
                                           int axis, bool& grounded, CollisionResult& outResult)
    {
        float velAxis = (axis == 0) ? velocity.x : (axis == 1) ? velocity.y : velocity.z;
        if (std::abs(velAxis) < EPSILON) return; // Движения нет, пропускаем

        float moveDelta = velAxis * dt;

        // === ЗАЩИТА ОТ ЛАГОВ ===
        // Ограничиваем максимальное смещение за кадр, чтобы при просадках FPS 
        // игрок не пролетал сквозь стены и цикл не выполнялся слишком долго
        constexpr float MAX_DELTA = 10.0f; 
        moveDelta = std::clamp(moveDelta, -MAX_DELTA, MAX_DELTA);

        // === ПРОСТРАНСТВЕННЫЙ SUB-STEPPING ===
        // Разбиваем движение на шаги не более SWEEP_STEP блоков.
        // При SWEEP_STEP = 0.5f гарантированно detecting столкновение со стеной толщиной 1 блок.
        constexpr float SWEEP_STEP = 0.5f;
        int steps = std::max(1, static_cast<int>(std::ceil(std::abs(moveDelta) / SWEEP_STEP)));
        float stepDelta = moveDelta / static_cast<float>(steps);

        // === BOUNDING BOX ПРОВЕРКА ===
        // Проверяем несколько уровней высоты с учётом радиуса.
        // Skin Width (radius - 0.02f) предотвращает застревание на углах блоков (corner snagging)
        auto checkCollisionAt = [&](const ax::Vec3& pos) -> bool
        {
            // 🔧 FIX #3: Расширенный набор уровней проверки по Y.
            // Добавлен уровень чуть ниже подошвы (-0.05f) — это критично при высоком FPS,
            // когда шаг движения за кадр < 0.01, и точка yOff=0.01 не попадает в блок.
            // Для отрицательных yOff проверяем только центр (без углов радиуса),
            // т.к. капсула не расширяется вниз — это probe, а не физическое расширение.
            float yLevels[] = { -0.05f, 0.01f, capsule.height * 0.5f, capsule.height - 0.01f };
            float r = capsule.radius - 0.02f; // Skin width

            for (float yOff : yLevels)
            {
                // 🔧 FIX #3: Для отрицательных yOff — только центральный probe
                if (yOff < 0.0f) {
                    if (isSolidAt(ax::Vec3(pos.x, pos.y + yOff, pos.z))) return true;
                    continue;
                }
                // 4 угла капсулы
                if (isSolidAt(pos + ax::Vec3( r, yOff,  r))) return true;
                if (isSolidAt(pos + ax::Vec3(-r, yOff,  r))) return true;
                if (isSolidAt(pos + ax::Vec3( r, yOff, -r))) return true;
                if (isSolidAt(pos + ax::Vec3(-r, yOff, -r))) return true;
                // Центр (для узких стен ровно по центру)
                if (isSolidAt(ax::Vec3(pos.x, pos.y + yOff, pos.z))) return true;
            }
            return false;
        };

        // === ИНКРЕМЕНТАЛЬНАЯ РАЗВЁРТКА ===
        for (int i = 0; i < steps; ++i)
        {
            ax::Vec3 testPos = outResult.resolvedPosition;
            if (axis == 0) testPos.x += stepDelta;
            else if (axis == 1) testPos.y += stepDelta;
            else testPos.z += stepDelta;

            if (checkCollisionAt(testPos))
            {
                // Коллизия обнаружена на этом шаге!
                float sign = (moveDelta > 0.0f) ? 1.0f : -1.0f;

                // Вычисляем нормаль для скольжения (wall sliding)
                outResult.collisionNormal = ax::Vec3(axis == 0 ? sign : 0, 
                                                     axis == 1 ? sign : 0, 
                                                     axis == 2 ? sign : 0);

                // Если коллизия снизу (ось Y, движение вниз), помечаем grounded
                if (axis == 1 && moveDelta < 0.0f)
                    grounded = true;

                // ВАЖНО: Не двигаем позицию! Остаёмся на outResult.resolvedPosition.
                // Прерываем цикл — движение по этой оси заблокировано.
                return;
            }

            // Если коллизии нет — принимаем шаг
            outResult.resolvedPosition = testPos;

            // Если двигаемся вниз и не уперлись в пол на этом шаге — сбрасываем grounded
            if (axis == 1 && moveDelta < 0.0f)
                grounded = false;
        }
    }
} // namespace voxel_collision