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
 *
 * ⚠ 使用边界（审查结论, 2026-09）:
 *   - 生产代码只使用 set()/get()/isCancelled() 三个成员（标志本身是原子的）;
 *   - setCancelCallback()/reset()/swap() **当前无任何调用者**, 且按现状启用是
 *     不安全的使用方式:
 *       * set() 会在**调用者线程**直接执行 m_cancelCallback, 而
 *         setCancelCallback() 对该 std::function 的赋值没有任何同步——两个线程
 *         分别 set/setCancelCallback 就是数据竞争（未定义行为）;
 *       * reset() 会连带清空回调, 语义上把"标志"和"回调注册"耦合在一起,
 *         多线程下会让回调在不同线程看到不一致的状态;
 *       * swap() 对 m_flag 用了 acq_rel 但只交换标志与回调, 不保证与
 *         其他成员的关联状态一致。
 *     如确需回调能力: 把 Callback 换成 std::atomic<std::shared_ptr<Callback>>
 *     （或加互斥锁保护), 并在 set() 中先取局部副本再调用。
 *   - 否则请勿在生产路径引入这三者, 也不要因为"看起来线程安全"而直接启用。
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
#ifdef XGZ_ENABLE_UNUSED_CANCEL_API
        if (value && m_cancelCallback) {
            m_cancelCallback();
        }
#endif
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
    
#ifdef XGZ_ENABLE_UNUSED_CANCEL_API
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
        bool mine = m_flag.load(std::memory_order_relaxed);
        bool theirs = other.m_flag.exchange(mine, std::memory_order_acq_rel);
        m_flag.store(theirs, std::memory_order_release);
        std::swap(m_cancelCallback, other.m_cancelCallback);
    }
#endif
    
private:
    std::atomic<bool> m_flag;
#ifdef XGZ_ENABLE_UNUSED_CANCEL_API
    Callback m_cancelCallback;
#endif
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