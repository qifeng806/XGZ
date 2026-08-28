#ifndef CORE_PROPAGATOR_H
#define CORE_PROPAGATOR_H
// =============================================================================
// Propagator.h — perturb::Satellite 轻量封装
// 将双精度儒略日转换为 perturb::JulianDate，调用 SGP4/SDP4 传播
// 返回 EciState（TEME 系），失败时返回 nullopt
//
// 线程安全: 非线程安全。底层 SGP4 内核在每次传播时改写 satrec 的
// atime/xli/xni/error 等成员（深空积分状态随调用推进）——同一实例
// 必须由单线程独占，并行预报请每个任务/线程各自构造独立实例
// =============================================================================
#include "DataTypes.h"
#include <memory>
#include <optional>
#include <string>

namespace perturb { class Satellite; }

class Propagator {
public:
    Propagator();
    ~Propagator();

    // 禁止拷贝（持有动态分配的 perturb::Satellite）
    Propagator(const Propagator &) = delete;
    Propagator &operator=(const Propagator &) = delete;

    // 允许移动
    Propagator(Propagator &&other) noexcept;
    Propagator &operator=(Propagator &&other) noexcept;

    // 从 TLE 两行初始化（字符串会被修改，传入副本）
    bool initialize(const std::string &line1, const std::string &line2,
                    std::string &errorMsg);

    bool isInitialized() const { return m_initialized; }

    // 核心传播：双精度儒略日 → TEME 位置/速度
    // 成功返回 EciState，失败（衰减/异常）返回 nullopt
    std::optional<EciState> propagate(double jd);

    // 暴露历元儒略日
    double epochJD() const { return m_epochJD; }

    // 暴露卫星名称
    const std::string &satelliteName() const { return m_name; }
    void setSatelliteName(const std::string &name) { m_name = name; }

    int noradID() const { return m_noradID; }
    Sgp4Type modelType() const { return m_modelType; }

private:
    // 唯一持有 perturb::Satellite（类型安全，替代 void* 手动转换）
    std::unique_ptr<perturb::Satellite> m_satellite;
    bool  m_initialized = false;
    double m_epochJD = 0.0;
    std::string m_name;
    int    m_noradID = 0;
    Sgp4Type m_modelType = Sgp4Type::LEO_SGP4;
};

#endif // CORE_PROPAGATOR_H
