// =============================================================================
// Propagator.cpp — perturb::Satellite 封装实现
// =============================================================================
#include "Propagator.h"
#include "perturb-master/include/perturb/perturb.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <stdexcept>

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
    
    // 添加详细的长度检查信息
    if (line1.length() != 69) {
        errorMsg += " (行1长度: " + std::to_string(line1.length()) + " 字符)";
    }
    if (line2.length() != 69) {
        errorMsg += " (行2长度: " + std::to_string(line2.length()) + " 字符)";
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
                errorMsg = "TLE 格式无效: TLE 数据不符合规范要求";
                errorMsg += "\n可能原因: ";
                errorMsg += "- TLE字段格式错误（如非数字字符、字段越界）";
                errorMsg += "\n- 轨道根数超出物理范围（如偏心率≥1、倾角>180°）";
                errorMsg += "\n- 校验和错误导致数据损坏";
                errorMsg += "\n- TLE版本与SGP4模型不兼容";
                break;
            case perturb::Sgp4Error::DECAYED:
                errorMsg = "卫星已衰减: 卫星轨道已衰减至大气层内";
                errorMsg += "\n可能原因: ";
                errorMsg += "- 卫星大气阻力过大导致轨道快速衰减";
                errorMsg += "\n- 使用了过时的TLE数据（历元时间过早）";
                errorMsg += "\n- B*阻力项参数异常";
                errorMsg += "\n建议: 使用更新的TLE数据或检查卫星状态";
                break;
            case perturb::Sgp4Error::MEAN_ELEMENTS:
                errorMsg = "平根数异常: 使用平根数而非瞬时根数";
                errorMsg += "\n可能原因: ";
                errorMsg += "- TLE使用了平均轨道根数而非瞬时根数";
                errorMsg += "\n- SGP4模型版本与TLE格式不匹配";
                errorMsg += "\n建议: 确认TLE格式或尝试使用不同SGP4实现";
                break;
            default:
                errorMsg = "SGP4 初始化错误: 未知错误代码 " + std::to_string(static_cast<int>(err));
                errorMsg += "\n可能原因: ";
                errorMsg += "- 未知或未处理的SGP4错误类型";
                errorMsg += "\n- perturb库版本与TLE格式不兼容";
                errorMsg += "\n- 内存分配或数值计算异常";
                errorMsg += "\n建议: 更新perturb库或联系技术支持";
                break;
            }
            
            return false;   // unique_ptr 自动释放
        }

        // 获取历元
        perturb::JulianDate epoch = sat->epoch();
        if (!std::isfinite(epoch.jd) || !std::isfinite(epoch.jd_frac)) {
            errorMsg = "历元数据无效: 儒略日数据不正常";
            return false;
        }
        m_epochJD = epoch.jd + epoch.jd_frac;
        if (!std::isfinite(m_epochJD)) {
            errorMsg = "历元计算错误: 儒略日计算结果异常";
            return false;
        }

        // NORAD 编号（elsetrec.satnum）与轨道模型类别（由平运动推导周期）
        //   no_unkozai: 未作 Kozai 修正的平运动 (rad/min) ⇒ 周期 = 2π/no_unkozai
        const auto &rec = sat->sat_rec;
        
        // 防御性解析 NORAD 编号
        {
            const char *b = rec.satnum, *e = rec.satnum + 5;
            while (b < e && (*b == ' ' || *b == '\t')) ++b;
            while (e > b && (e[-1] == ' ' || e[-1] == '\t')) --e;
            int nid = 0;
            const auto res = std::from_chars(b, e, nid);
            if (res.ec == std::errc() && res.ptr == e) {
                m_noradID = nid;
                if (m_noradID <= 0) {
                    errorMsg = "NORAD 编号无效: 编号必须为正整数";
                    return false;
                }
            } else {
                m_noradID = 0;
                errorMsg = "NORAD 编号解析失败: 无法从 '" + std::string(rec.satnum) + "' 解析有效编号";
                return false;
            }
        }
        
        if (m_name.empty()) {
            m_name = rec.satnum;
            // 清理名称中的空白字符
            m_name.erase(std::remove_if(m_name.begin(), m_name.end(), 
                                      [](char c) { return std::isspace(c); }), m_name.end());
            if (m_name.empty()) {
                m_name = "Unknown Satellite";
            }
        }
        
        // 检查平运动数据有效性
        if (!std::isfinite(rec.no_unkozai)) {
            errorMsg = "平运动数据无效: 平运动值不是有限数值";
            return false;
        }
        
        const double periodMin = (rec.no_unkozai > 1e-12)
            ? (2.0 * 3.14159265358979323846) / rec.no_unkozai : 0.0;
            
        if (!std::isfinite(periodMin) || periodMin <= 0.0) {
            errorMsg = "轨道周期计算错误: 计算得到的周期无效";
            return false;
        }
        
        // 分类带: 阈值与 TleParser::classifyModel 对齐（225/1400 min）,
        // 但基准不同——此处用 Kozai 修正后平运动 no_unkozai 推周期（SGP4
        // 实际采用, 更准）, 解析器用目录平运动 meanMotionRevDay。近边界
        // 轨道两处标签可能相差一个 Kozai 修正量级; 仅影响展示字段
        //（ephemerisType 标签）, 不影响传播路径本身
        if (periodMin < 225.0)          m_modelType = Sgp4Type::LEO_SGP4;
        else if (periodMin < 1400.0)    m_modelType = Sgp4Type::MEO_SDP4;
        else                            m_modelType = Sgp4Type::GEO_SDP4_DS;

        m_satellite = std::move(sat);
    } catch (const std::invalid_argument &e) {
        errorMsg = "参数错误: " + std::string(e.what());
        return false;
    } catch (const std::out_of_range &e) {
        errorMsg = "范围错误: " + std::string(e.what());
        return false;
    } catch (const std::exception &e) {
        errorMsg = "perturb 标准异常: " + std::string(e.what());
        return false;
    } catch (...) {
        errorMsg = "perturb 未知异常: 未知类型的异常被抛出";
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
