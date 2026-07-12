// train_generator.h — 根据线路和站点自动生成列车种子数据
#pragma once

#include "models.h"

#include <vector>

/**
 * TrainGenerator — 根据线路和站点信息生成合理的列车数据。
 * 每条线路生成 ~10 趟列车，总计约 100 趟。
 * 若 trains.json 已存在则跳过，确保幂等。
 */
class TrainGenerator {
public:
    /** 根据线路和站点生成列车数据（约 100 辆） */
    static std::vector<Train> generate(
        const std::vector<Line>& lines,
        const std::vector<Station>& stations);

private:
    // ── 内部方法 ──

    /** 为一条线路生成两个方向的列车 */
    static void generateForLine(
        const Line& line,
        const std::vector<Station>& stations,
        std::vector<Train>& out_trains,
        int& train_counter);

};
