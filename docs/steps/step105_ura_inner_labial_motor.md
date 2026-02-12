# Step 105: URA 内唇运动神经元 — 鼻部定位/觅食通路

> 日期: 2026-02-13

---

## 动机

鼻触回路已有 OLQ/IL1→RMD 直接通路，但 Emmons 2024 指出 IL1/IL2 还通过 **URA 运动神经元**驱动头部肌肉。URA 是内唇感觉-运动通路的关键中继，添加后完善 Community 2 (Foraging) 的运动输出。

## 生物学基础

### URA 分类与特性
- **White 1986 分类**: 运动神经元（motor neuron），4 象限
- **Community 2 (Foraging)**: 与 IL1/IL2 同组
- **NMJs**: 在 nerve ring 制造神经肌肉接头 → 头部体壁肌肉
- **感觉特性**: 树突延伸至鼻部 → 可能有感觉功能 ("unknown receptor")
- **递质**: 胆碱能 (ACh, Pereira 2015)

### 功能通路
> "IL1 and IL2 assess chemical and tactile information near or at the nose for positioning the nose in foraging; both target the body wall muscles in the head via head motor neurons RME and **URA**" — Emmons 2024

```
IL1 (鼻触觉) ─→ URA ─→ 头部肌肉 (NMJ)
IL2 (鼻化学) ─→ URA ─→ RMD (协调)
```

## 实现

### 新增神经元: 4 个
| 神经元 | 类型 | 递质 | 功能 |
|--------|------|------|------|
| URADL/DR/VL/VR | 运动 | ACh | 鼻部定位, 觅食头部运动 |

### 新增突触: 12 条
| 连接 | 数量 | 强度 | 来源 |
|------|------|------|------|
| IL1→URA | 4 | 2 sections | Emmons 2024, White 1986 |
| IL2→URA | 4 | 1 section | Emmons 2024 |
| URA→RMD | 4 | 1 section | White 1986 |

### 运动映射: +4 (87→91)
- URA: segments 0-3 (鼻尖最前端，比 SMD 更前)

## 更新的系统计数
| 指标 | 旧值 | 新值 |
|------|------|------|
| 神经元 | 226 | **230** |
| 运动神经元 | 104 | **108** |
| 化学突触 | 557 | **569** |
| 间隙连接 | 193 | 193 (不变) |
| 运动映射 | 87 | **91** |

## 修改文件

- `src/connectome/connectome_builder.cpp`: 注册 URA(4) + IL1/IL2→URA→RMD 突触
- `src/motor/motor_controller.cpp`: URA 鼻部段映射 (0-3)
- `src/simulation/regression_test.cpp`: 更新基线 (230/569/193)

## 参考文献

- White 1986 Phil Trans R Soc — URA 神经解剖
- Emmons 2024 PLOS Biology (PMC10983851) — URA 功能: IL1/IL2→URA→头部肌肉
- Pereira 2015 eLife — URA 胆碱能身份
- Hart 1995 — 鼻触回路 (OLQ/IL1/RMD)
