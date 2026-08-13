// =============================================================================
// Propagator.cpp — perturb::Satellite 封装实现
// =============================================================================
#include "Propagator.h"
#include "perturb-master/include/perturb/perturb.hpp"
#include <charconv>
#include <cmath>

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

    // 标准 TLE 两行根数必须是 69 字符（列对齐: 末位为校验和）。
    // 历史上曾用空格补齐短行——那会掩盖数据损坏并让底层按错列解析;
    // 与 TleParser::BAD_LENGTH 判据对齐, 显式拒绝
    if (line1.length() < 69 || line2.length() < 69) {
        errorMsg = "TLE 行长度不足 69 字符";
        return false;
    }

    // perturb::Satellite::from_tle 接受非 const string 引用（底层会修改）
    std::string l1 = line1;
    std::string l2 = line2;

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
        // atoi 对非法字符静默截断为 0——改为整段严格解析（satnum 为 5 位数字
        // 字段 + NUL, char[6]）。非法输入上游已被 TleParser 的 BAD_NORAD
        // 拦截, 此处仅防御性, 失败记 0
        {
            const char *b = rec.satnum, *e = rec.satnum + 5;
            while (b < e && (*b == ' ' || *b == '\t')) ++b;
            while (e > b && (e[-1] == ' ' || e[-1] == '\t')) --e;
            int nid = 0;
            const auto res = std::from_chars(b, e, nid);
            m_noradID = (res.ec == std::errc() && res.ptr == e) ? nid : 0;
        }
        if (m_name.empty()) m_name = rec.satnum;
        const double periodMin = (rec.no_unkozai > 1e-12)
            ? (2.0 * 3.14159265358979323846) / rec.no_unkozai : 0.0;
        // 分类带: 与 TleParser::classifyModel 同阈值、同基准（no_unkozai 周期）,
        // 避免 225min 边界处两处标签打架:
        //   < 225 min → LEO;  [225, 1400) min → MEO;
        //   ≥ 1400 min → GEO（同步带 1436min 及其上下漂移轨道/超同步轨道均属此带）
        if (periodMin < 225.0)          m_modelType = Sgp4Type::LEO_SGP4;
        else if (periodMin < 1400.0)    m_modelType = Sgp4Type::MEO_SDP4;
        else                            m_modelType = Sgp4Type::GEO_SDP4_DS;

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
