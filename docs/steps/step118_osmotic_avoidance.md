# Step 118: 渗透压回避 (Osmotic Avoidance)

> 日期: 2026-02-13

---

## 动机

C. elegans 能检测和回避高渗透压环境（如甘油环），这是经典的行为学实验范式（glycerol ring assay）。实现此行为需要：
1. 环境中的渗透压场（环形屏障）
2. ASH/ADL 通过 OSM-9/TRPV 通道感知渗透压
3. 通过已有 ASH→AIB→AVA 回路驱动急转回避

## 生物学基础

### 急性渗透压回避 (≥1 Osm)
- **ASH**: 主要渗透压感受器，开放到外界
- **OSM-9**: TRPV 通道，ASH 中的渗透压转导子
- **OCR-2**: 与 OSM-9 形成异聚体
- **ADL**: 次要渗透压感受器（贡献约 15%）
- **反应**: 接触高渗区 → ASH 放电 → AIB→AVA → 即刻后退

### 渐进渗透压厌恶 (轻度上升, ~400 mOsm)
- **URX/AQR/PQR**: 体腔感受器，暴露于体液
- **TAX-2/TAX-4**: cGMP 门控通道
- **反应**: 数分钟内转弯频率逐渐增加
- *（本步实现急性回避；渐进厌恶可未来扩展）*

### 参考文献
- Colbert 1997 — OSM-9 TRPV in ASH for osmotic avoidance
- Hilliard 2005 — glycerol ring assay, ASH + ADL contribution
- Liedtke 2003 — mammalian TRPV4 rescues osm-9 in ASH
- Yu 2017 eNeuro — osmotic upshift aversion via URX/TAX-2

## 实现细节

### 1. 环境：渗透压环形屏障
```
T(pos) = strength × exp(-d²/(2σ²))
d = |r(pos) - radius|   // 到环中心线的距离
σ = width / √2           // 屏障宽度
```
- 中心: arena 中心 (25, 25)
- 半径: 15mm
- 宽度: 1.5mm（锐利屏障）
- 强度: 1.0（最大渗透压）

### 2. ASH 渗透压感知
```
osm_at_head = sample_osmolarity(head_pos)
ASH: +60 pA × osm_at_head    // 主感受器
ADL: +15 pA × osm_at_head    // 次感受器 (~25% of ASH)
```

### 3. 回避回路
已有连接自动处理：
- ASH → AIB → AVA → 后退
- ASH → AVA/AVD → 直接后退
- ADL → AVA/AVD → 辅助后退

### CLI 参数
```
--osm    启用渗透压屏障环（甘油环实验）
```

## Diag 验证 (--osm --duration 120)

- wall proximity = 0/1200（虫子未到达墙壁 → 被渗透压环困住）
- ASH I_ext = 35 pA（渗透压激活）
- CI = 0.333（在渗透压环内正常趋化）

## 修改文件
- `src/environment/environment.h`: osmotic barrier 成员 + API
- `src/environment/environment.cpp`: sample_osmolarity() + set_osmotic_barrier()
- `src/simulation/apply_sensory_systems.cpp`: ASH/ADL OSM-9 渗透压感知
- `src/simulation/diag_main.cpp`: --osm CLI 参数
