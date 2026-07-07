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
    /**
     * 生成列车数据。
     * @param lines    线路列表
     * @param stations 站点列表
     * @return 生成的列车列表（100 辆左右）
     */
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
