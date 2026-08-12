// =============================================================================
// Propagator.cpp — perturb::Satellite 封装实现
// =============================================================================
#include "Propagator.h"
#include "perturb-master/include/perturb/perturb.hpp"
#include <cmath>
#include <cstdlib>

Propagator::Propagator() {}
Propagator::~Propagator() {}   // unique_ptr 自动释放（此处需完整类型，已在下方 include）

Propagator::Propagator(Propagator &&other) noexcept
    : m_satellite(std::move(other.m_satellite))
    , m_initialized(other.m_initialized)
    , m_epochJD(other.m_epochJD)
    , m_name(std::move(other.m_name))
    , m_noradID(other.m_noradID)
    , m_modelType(other.m_modelType)
{
    other.m_initialized = false;
}

Propagator &Propagator::operator=(Propagator &&other) noexcept
{
    if (this != &other) {
        m_satellite = std::move(other.m_satellite);
        m_initialized = other.m_initialized;
        m_epochJD = other.m_epochJD;
        m_name = std::move(other.m_name);
        m_noradID = other.m_noradID;
        m_modelType = other.m_modelType;
        other.m_initialized = false;
    }
    return *this;
}

bool Propagator::initialize(const std::string &line1, const std::string &line2,
                             std::string &errorMsg)
{
    // 先清理旧实例
    m_satellite.reset();
    m_initialized = false;

    // perturb::Satellite::from_tle 接受非 const string 引用（底层会修改）
    std::string l1 = line1;
    std::string l2 = line2;

    // 确保长度至少 69
    if (l1.length() < 69) l1.append(69 - l1.length(), ' ');
    if (l2.length() < 69) l2.append(69 - l2.length(), ' ');

    try {
        auto sat = std::make_unique<perturb::Satellite>(
            perturb::Satellite::from_tle(l1, l2, perturb::GravModel::WGS72));

        auto err = sat->last_error();
        if (err != perturb::Sgp4Error::NONE) {
            switch (err) {
            case perturb::Sgp4Error::INVALID_TLE:
                errorMsg = "TLE 格式无效"; break;
            case perturb::Sgp4Error::DECAYED:
                errorMsg = "卫星已衰减"; break;
            case perturb::Sgp4Error::MEAN_ELEMENTS:
                errorMsg = "平根数异常"; break;
            default:
                errorMsg = "SGP4 初始化错误"; break;
            }
            return false;   // unique_ptr 自动释放
        }

        // 获取历元
        perturb::JulianDate epoch = sat->epoch();
        m_epochJD = epoch.jd + epoch.jd_frac;

        // NORAD 编号（elsetrec.satnum）与轨道模型类别（由平运动推导周期）
        //   no_unkozai: 未作 Kozai 修正的平运动 (rad/min) ⇒ 周期 = 2π/no_unkozai
        const auto &rec = sat->sat_rec;
        m_noradID = std::atoi(rec.satnum);
        if (m_name.empty()) m_name = rec.satnum;
        const double periodMin = (rec.no_unkozai > 1e-12)
            ? (2.0 * 3.14159265358979323846) / rec.no_unkozai : 0.0;
        if (periodMin < 225.0)         m_modelType = Sgp4Type::LEO_SGP4;
        else if (periodMin < 1440.0)   m_modelType = Sgp4Type::MEO_SDP4;
        else                           m_modelType = Sgp4Type::GEO_SDP4_DS;

        m_satellite = std::move(sat);
    } catch (const std::exception &e) {
        errorMsg = std::string("perturb 异常: ") + e.what();
        return false;
    } catch (...) {
        errorMsg = "perturb 未知异常";
        return false;
    }

    m_initialized = true;
    return true;
}

std::optional<EciState> Propagator::propagate(double jd)
{
    if (!m_satellite || !m_initialized)
        return std::nullopt;

    auto *sat = m_satellite.get();

    // double JD → perturb::JulianDate
    perturb::JulianDate targetJD(jd);

    perturb::StateVector sv;
    perturb::Sgp4Error err = sat->propagate(targetJD, sv);

    if (err != perturb::Sgp4Error::NONE)
        return std::nullopt;

    // 防御: 数值异常（位置/速度非有限）时按失败处理,
    // 防止 NaN 污染后续过境几何/光照计算
    if (!std::isfinite(sv.position[0]) || !std::isfinite(sv.position[1])
        || !std::isfinite(sv.position[2])
        || !std::isfinite(sv.velocity[0]) || !std::isfinite(sv.velocity[1])
        || !std::isfinite(sv.velocity[2]))
        return std::nullopt;

    EciState result;
    result.posKm  = sv.position;
    result.velKms = sv.velocity;
    return result;
}
