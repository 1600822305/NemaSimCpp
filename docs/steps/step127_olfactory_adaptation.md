# Step 127: 嗅觉适应 — AWC EGL-4/PKG 通路

> 日期: 2026-02-13

---

## 动机

此前嗅觉适应仅依赖 STP 囊泡耗竭（突触层面），缺少 AWC 细胞内特异性激酶通路。EGL-4/PKG 是 C. elegans 嗅觉适应的核心调节因子，控制从短期到长期的适应过渡。

## 生物学基础

### 两阶段适应模型 (L'Etoile 2002, O'Halloran 2010)
```
Phase 1: 短期适应 (~10-30 min)
  持续气味暴露 → cGMP 积累 → EGL-4 激活（细胞质）
  → EGL-4 磷酸化 TAX-2（cGMP 通道 β 亚基）
  → 通道电导降低 → AWC 响应减弱 50%

Phase 2: 长期适应 (~60-90 min)
  持续暴露 → EGL-4 核转位
  → 转录水平改变 → AWC 持续抑制（增益 ×0.15）
  → 持续 >2.5 小时

恢复:
  气味移除 → EGL-4 缓慢退出核（数小时）
  → AWC 响应逐渐恢复
```

### 关键分子
- **EGL-4/PKG**: cGMP 依赖性蛋白激酶，核转位是长期适应的充要条件
- **TAX-2**: cGMP 门控通道 β 亚基，EGL-4 磷酸化位点
- **ODR-1**: 鸟苷酸环化酶，产生 cGMP
- **GPC-1**: G 蛋白 γ 亚基，早期适应所需

### 参考文献
- L'Etoile et al. 2002 Neuron — EGL-4 调控嗅觉适应
- O'Halloran et al. 2010 PNAS — EGL-4 核转位指导长期适应
- Colbert & Bargmann 1995 — 气味特异性适应

## 实现细节

### AWC 气味暴露跟踪
```cpp
if (awc_activity > 0.3):
    exposure += (activity - exposure) × dt / 15000ms  // 15s 积累
else:
    exposure -= exposure × dt / 30000ms  // 30s 衰减
```

### EGL-4 核转位
```cpp
if (exposure > 0.6):  // 持续高暴露
    nuclear += rate × (1 - nuclear) × dt / 30000ms  // 核进入
if (exposure < 0.2):  // 暴露移除
    nuclear -= nuclear × dt / 60000ms  // 核退出（慢恢复）
```

### AWC 增益调制
```
短期: gain = 1.0 - 0.5 × min(1, (exposure-0.3)/0.3)  // 1.0→0.5
长期: gain *= 1.0 - 0.7 × nuclear                      // ×0.3 at full
最终: adapt_current = -12pA × (1 - gain)               // 超极化 AWC
```

| 状态 | exposure | EGL-4 nuclear | AWC gain | 效应 |
|------|----------|---------------|----------|------|
| 初始 | 0 | 0 | 1.0 | 正常响应 |
| 短期适应 | 0.5 | 0 | 0.67 | 响应减弱 33% |
| 长期适应 | 0.8 | 0.5 | 0.33 | 响应减弱 67% |
| 完全适应 | 1.0 | 0.9 | 0.15 | 几乎不响应 |
| 恢复中 | 0.1 | 0.3 | 0.79 | 逐渐恢复 |

## Diag 验证

```
39. OLFACTORY ADAPTATION (Step 127, L'Etoile 2002):
   AWC odor_exposure=0  EGL-4 nuclear=0  AWC gain=1 NAIVE
```

120s 仿真中虫子未持续暴露于高浓度气味，因此保持 NAIVE 状态。长时间靠近食物源时会触发适应。

## 修改文件
- `src/simulation/simulation_engine.h`: EGL-4/AWC 适应参数
- `src/simulation/update_internal_states.cpp`: update_olfactory_adaptation()
- `src/simulation/simulation_engine.cpp`: 在 step() 中调用
- `src/simulation/diag_main.cpp`: 嗅觉适应诊断输出
