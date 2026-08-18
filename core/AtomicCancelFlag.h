#ifndef CORE_ATOMICCANCELFLAG_H
#define CORE_ATOMICCANCELFLAG_H
// =============================================================================
// AtomicCancelFlag.h — 线程安全的取消标志管理
// =============================================================================
#include <atomic>
#include <memory>
#include <functional>

/**
 * @brief 线程安全的取消标志管理类
 * 
 * 提供了一个线程安全的取消标志，支持 RAII 管理和回调函数。
 * 主要用于 OrbitCalculator 中的协作式取消机制。
 */
class AtomicCancelFlag {
public:
    using Callback = std::function<void()>;
    
    /**
     * @brief 构造函数
     * @param initialValue 初始值（默认为 false）
     */
    explicit AtomicCancelFlag(bool initialValue = false) 
        : m_flag(initialValue) {}
    
    /**
     * @brief 设置取消状态
     * @param value true 表示取消，false 表示未取消
     * @param memoryOrder 内存序，默认为 release
     */
    void set(bool value, std::memory_order memoryOrder = std::memory_order_release) {
        m_flag.store(value, memoryOrder);
        if (value && m_cancelCallback) {
            m_cancelCallback();
        }
    }
    
    /**
     * @brief 获取取消状态
     * @param memoryOrder 内存序，默认为 acquire
     * @return true 表示已取消，false 表示未取消
     */
    bool get(std::memory_order memoryOrder = std::memory_order_acquire) const {
        return m_flag.load(memoryOrder);
    }
    
    /**
     * @brief 检查是否已取消
     * @return true 表示已取消，false 表示未取消
     */
    bool isCancelled() const {
        return get();
    }
    
    /**
     * @brief 设置取消回调函数
     * @param callback 回调函数
     */
    void setCancelCallback(Callback callback) {
        m_cancelCallback = callback;
    }
    
    /**
     * @brief 重置取消标志
     */
    void reset() {
        m_flag.store(false, std::memory_order_release);
        m_cancelCallback = nullptr;
    }
    
    /**
     * @brief 交换取消状态
     * @param other 另一个取消标志
     */
    void swap(AtomicCancelFlag& other) noexcept {
        // 双向对换: 单次 exchange 只把 other 拷入 this 并丢弃旧值,
        // 必须再写回 other 才是交换语义
        bool mine = m_flag.load(std::memory_order_relaxed);
        bool theirs = other.m_flag.exchange(mine, std::memory_order_acq_rel);
        m_flag.store(theirs, std::memory_order_release);
        std::swap(m_cancelCallback, other.m_cancelCallback);
    }
    
private:
    std::atomic<bool> m_flag;
    Callback m_cancelCallback;
};

/**
 * @brief 取消标志的共享指针类型
 * 
 * 使用 shared_ptr 确保取消标志的生命周期安全
 */
using AtomicCancelFlagPtr = std::shared_ptr<AtomicCancelFlag>;

/**
 * @brief 创建取消标志的工厂函数
 * @param initialValue 初始值
 * @return 取消标志的共享指针
 */
inline AtomicCancelFlagPtr makeAtomicCancelFlag(bool initialValue = false) {
    return std::make_shared<AtomicCancelFlag>(initialValue);
}

#endif // CORE_ATOMICCANCELFLAG_H