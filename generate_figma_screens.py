import os
import math
from PIL import Image, ImageDraw, ImageFont

# Canvas Setup for 6 Screens
CANVAS_W = 2800
CANVAS_H = 1200
PHONE_W = 370
PHONE_H = 780
CORNER_R = 38
SPACING = 55
START_X = (CANVAS_W - (6 * PHONE_W + 5 * SPACING)) // 2
START_Y = 220

# Colors
BG_CANVAS = (18, 20, 26)
DOT_COLOR = (32, 36, 48)
PHONE_BG = (11, 19, 43)
PHONE_BORDER = (45, 55, 78)
CARD_BG = (22, 33, 62)
CARD_BORDER = (40, 56, 92)

COLOR_PRIMARY = (72, 202, 228)    # Cyan
COLOR_ACCENT = (0, 119, 182)      # Deep Cyan
COLOR_SAFE = (6, 214, 160)        # Mint Green
COLOR_WARN = (255, 209, 102)      # Amber
COLOR_ALERT = (239, 71, 111)      # Coral Red
COLOR_TURBO = (255, 75, 110)      # Neon Crimson for Turbo
COLOR_TEXT_MAIN = (255, 255, 255)
COLOR_TEXT_MUTED = (140, 153, 174)

canvas = Image.new('RGB', (CANVAS_W, CANVAS_H), BG_CANVAS)
draw = ImageDraw.Draw(canvas)

# Dot Grid
for x in range(0, CANVAS_W, 30):
    for y in range(0, CANVAS_H, 30):
        draw.point((x, y), fill=DOT_COLOR)

# Load Fonts
try:
    font_title = ImageFont.truetype("segoeui.ttf", 34)
    font_sub = ImageFont.truetype("segoeui.ttf", 17)
    font_label = ImageFont.truetype("segoeuib.ttf", 15)
    font_h1 = ImageFont.truetype("segoeuib.ttf", 24)
    font_h2 = ImageFont.truetype("segoeuib.ttf", 19)
    font_large = ImageFont.truetype("segoeuib.ttf", 48)
    font_body = ImageFont.truetype("segoeui.ttf", 13)
    font_body_b = ImageFont.truetype("segoeuib.ttf", 13)
    font_small = ImageFont.truetype("segoeui.ttf", 11)
    font_small_b = ImageFont.truetype("segoeuib.ttf", 11)
except:
    font_title = font_sub = font_label = font_h1 = font_h2 = font_large = font_body = font_body_b = font_small = font_small_b = ImageFont.load_default()

# Header on Canvas
draw.text((START_X, 50), "MediCool · Advanced Mobile App Architecture & Screen Hierarchy", fill=COLOR_TEXT_MAIN, font=font_title)
draw.text((START_X, 95), "Tabs: Telemetry  |  Advanced Fans  |  100% Turbo Overdrive  |  Setpoints  |  Audit Log", fill=COLOR_PRIMARY, font=font_sub)

screen_titles = [
    "01 · PAIRING & CONNECT",
    "02 · TELEMETRY DASHBOARD",
    "03 · ADVANCED FAN CONTROL",
    "04 · 100% TURBO OVERDRIVE",
    "05 · PRESETS & SETPOINT",
    "06 · 24H AUDIT & REPORT"
]

def draw_phone_base(x, y, active_tab_idx=0):
    for i in range(8, 0, -2):
        draw.rounded_rectangle([x-i, y-i, x+PHONE_W+i, y+PHONE_H+i], radius=CORNER_R+i, fill=(14, 16, 22))
    draw.rounded_rectangle([x, y, x+PHONE_W, y+PHONE_H], radius=CORNER_R, fill=PHONE_BG, outline=PHONE_BORDER, width=2)
    
    # Dynamic Island & Status Bar
    di_w, di_h = 95, 22
    di_x = x + (PHONE_W - di_w)//2
    draw.rounded_rectangle([di_x, y+10, di_x+di_w, y+10+di_h], radius=11, fill=(0, 0, 0))
    draw.text((x + 26, y + 14), "09:41", fill=COLOR_TEXT_MAIN, font=font_small_b)
    draw.text((x + PHONE_W - 55, y + 14), "5G 84%", fill=COLOR_TEXT_MUTED, font=font_small)

    # Bottom Tab Bar (for screens 2 to 6)
    if active_tab_idx > 0:
        by = y + PHONE_H - 58
        draw.line([x, by, x + PHONE_W, by], fill=CARD_BORDER, width=1)
        tabs = ["Home", "Fans", "Turbo", "Modes", "Audit"]
        tw = PHONE_W // 5
        for t_i, t_name in enumerate(tabs):
            tx = x + t_i * tw
            is_active = (t_i + 1 == active_tab_idx)
            t_col = COLOR_PRIMARY if is_active else COLOR_TEXT_MUTED
            draw.text((tx + 14, by + 16), t_name, fill=t_col, font=font_small_b if is_active else font_small)
            if is_active:
                draw.rounded_rectangle([tx + 18, by + 34, tx + tw - 18, by + 36], radius=1, fill=COLOR_PRIMARY)
    else:
        # Home bar only
        draw.rounded_rectangle([x + (PHONE_W-120)//2, y + PHONE_H - 12, x + (PHONE_W+120)//2, y + PHONE_H - 7], radius=3, fill=(100, 110, 130))

for idx in range(6):
    px = START_X + idx * (PHONE_W + SPACING)
    py = START_Y
    draw.text((px, py - 38), screen_titles[idx], fill=COLOR_PRIMARY, font=font_label)
    draw_phone_base(px, py, active_tab_idx=idx)

    # SCREEN 1: PAIRING
    if idx == 0:
        cy = py + 180
        draw.ellipse([px + 125, cy - 50, px + 245, cy + 70], fill=CARD_BG, outline=COLOR_PRIMARY, width=2)
        draw.text((px + 165, cy - 20), "*", fill=COLOR_PRIMARY, font=font_large)
        draw.text((px + 115, cy + 90), "MEDICOOL", fill=COLOR_TEXT_MAIN, font=font_h1)
        draw.text((px + 80, cy + 125), "WHO Standard 2.0C - 8.0C", fill=COLOR_PRIMARY, font=font_small)

        # Discovered Card
        dy = py + 380
        draw.rounded_rectangle([px + 20, dy, px + PHONE_W - 20, dy + 110], radius=16, fill=CARD_BG, outline=COLOR_PRIMARY, width=2)
        draw.text((px + 36, dy + 16), "MediCool-IoT #0887", fill=COLOR_TEXT_MAIN, font=font_body_b)
        draw.text((px + 36, dy + 38), "3S 10Ah Li-Ion  ·  FW v2.0", fill=COLOR_TEXT_MUTED, font=font_small)
        draw.text((px + 36, dy + 58), "Signal: -54 dBm (Connected)", fill=COLOR_SAFE, font=font_small_b)
        draw.rounded_rectangle([px + PHONE_W - 125, dy + 62, px + PHONE_W - 35, dy + 95], radius=10, fill=COLOR_PRIMARY)
        draw.text((px + PHONE_W - 110, dy + 72), "OPEN APP", fill=(10, 20, 40), font=font_small_b)

    # SCREEN 2: TELEMETRY DASHBOARD (Tab 1)
    elif idx == 1:
        draw.text((px + 20, py + 48), "MediCool #0887", fill=COLOR_TEXT_MAIN, font=font_h2)
        draw.rounded_rectangle([px + PHONE_W - 110, py + 48, px + PHONE_W - 20, py + 70], radius=11, fill=(15, 45, 40), outline=COLOR_SAFE)
        draw.text((px + PHONE_W - 98, py + 52), "CONNECTED", fill=COLOR_SAFE, font=font_small_b)

        # Hero Gauge
        gy = py + 85
        draw.rounded_rectangle([px + 20, gy, px + PHONE_W - 20, gy + 175], radius=18, fill=CARD_BG, outline=CARD_BORDER)
        draw.text((px + 105, gy + 14), "CHAMBER TEMPERATURE", fill=COLOR_TEXT_MUTED, font=font_small_b)
        draw.text((px + 115, gy + 38), "4.2", fill=COLOR_TEXT_MAIN, font=font_large)
        draw.text((px + 205, gy + 48), "C", fill=COLOR_PRIMARY, font=font_h1)
        draw.rounded_rectangle([px + 75, gy + 110, px + PHONE_W - 75, gy + 138], radius=14, fill=(15, 50, 42), outline=COLOR_SAFE)
        draw.text((px + 95, gy + 118), "● WHO SAFE (2-8 C)", fill=COLOR_SAFE, font=font_small_b)
        draw.text((px + 90, gy + 150), "Target: 4.0 C (INSULIN)", fill=COLOR_PRIMARY, font=font_small)

        # Quick Live Fan Chips
        fy = gy + 190
        draw.rounded_rectangle([px + 20, fy, px + PHONE_W - 20, fy + 70], radius=14, fill=CARD_BG, outline=CARD_BORDER)
        draw.text((px + 32, fy + 12), "LIVE FAN STATUS", fill=COLOR_TEXT_MUTED, font=font_small_b)
        draw.text((px + 32, fy + 34), "Hot Blower: 58% (Auto)", fill=COLOR_TEXT_MAIN, font=font_small)
        draw.text((px + PHONE_W - 145, fy + 34), "Cold Blower: ON", fill=COLOR_SAFE, font=font_small_b)
        draw.text((px + 32, fy + 50), "Destratification: Active (Delta 0.4C)", fill=COLOR_PRIMARY, font=font_small)

        # Battery Card
        by = fy + 82
        draw.rounded_rectangle([px + 20, by, px + PHONE_W - 20, by + 95], radius=16, fill=CARD_BG, outline=CARD_BORDER)
        draw.text((px + 32, by + 12), "BATTERY (12.18V)", fill=COLOR_TEXT_MAIN, font=font_body_b)
        draw.text((px + PHONE_W - 75, by + 12), "84%", fill=COLOR_PRIMARY, font=font_body_b)
        draw.rounded_rectangle([px + 32, by + 34, px + PHONE_W - 32, by + 42], radius=4, fill=(35, 45, 65))
        draw.rounded_rectangle([px + 32, by + 34, px + 32 + int((PHONE_W-64)*0.84), by + 42], radius=4, fill=COLOR_SAFE)
        draw.text((px + 32, by + 52), "PWR: 20V PD [Chg 47% DPM]", fill=COLOR_TEXT_MUTED, font=font_small)
        draw.text((px + 32, by + 70), "Est. Runtime: 11h 45m left", fill=COLOR_PRIMARY, font=font_small)

        # Quick Preset Chips
        cy = by + 108
        chips = ["Insulin 4", "Vaccine 5", "Drops 6"]
        cw = (PHONE_W - 40 - 16) // 3
        for i, c in enumerate(chips):
            cx = px + 20 + i*(cw + 8)
            c_bg = COLOR_PRIMARY if i == 0 else CARD_BG
            c_text = (10, 20, 40) if i == 0 else COLOR_TEXT_MAIN
            draw.rounded_rectangle([cx, cy, cx + cw, cy + 38], radius=10, fill=c_bg, outline=CARD_BORDER)
            draw.text((cx + 12, cy + 11), c, fill=c_text, font=font_small_b)

    # SCREEN 3: ADVANCED FAN CONTROL (Tab 2)
    elif idx == 2:
        draw.text((px + 20, py + 48), "Advanced Fan Controls", fill=COLOR_TEXT_MAIN, font=font_h2)
        draw.text((px + 20, py + 74), "Manual override & RPM management", fill=COLOR_TEXT_MUTED, font=font_small)

        # HOT FAN CARD
        hfy = py + 105
        draw.rounded_rectangle([px + 20, hfy, px + PHONE_W - 20, hfy + 185], radius=18, fill=CARD_BG, outline=CARD_BORDER)
        draw.text((px + 35, hfy + 14), "HOT SIDE BLOWER (6028/7530)", fill=COLOR_TEXT_MAIN, font=font_body_b)
        draw.text((px + 35, hfy + 34), "Current: 58% PWM  ·  Heatsink: 38.6C", fill=COLOR_TEXT_MUTED, font=font_small)
        
        # Mode Toggle (Auto vs Manual)
        draw.rounded_rectangle([px + 35, hfy + 56, px + 120, hfy + 82], radius=8, fill=(20, 45, 70), outline=COLOR_PRIMARY)
        draw.text((px + 45, hfy + 62), "AUTO GOV", fill=COLOR_PRIMARY, font=font_small_b)
        draw.rounded_rectangle([px + 130, hfy + 56, px + 215, hfy + 82], radius=8, fill=CARD_BG, outline=CARD_BORDER)
        draw.text((px + 145, hfy + 62), "MANUAL", fill=COLOR_TEXT_MUTED, font=font_small)

        # Fan Slider (0% to 100%)
        draw.text((px + 35, hfy + 100), "Manual Override Slider:", fill=COLOR_TEXT_MUTED, font=font_small)
        draw.rounded_rectangle([px + 35, hfy + 124, px + PHONE_W - 35, hfy + 132], radius=4, fill=(35, 45, 65))
        draw.rounded_rectangle([px + 35, hfy + 124, px + 35 + int((PHONE_W-70)*0.75), hfy + 132], radius=4, fill=COLOR_PRIMARY)
        draw.ellipse([px + 35 + int((PHONE_W-70)*0.75) - 8, hfy + 118, px + 35 + int((PHONE_W-70)*0.75) + 12, hfy + 138], fill=(255, 255, 255))
        draw.text((px + 35, hfy + 146), "Locked to: 75% Force Exhaust", fill=COLOR_PRIMARY, font=font_small_b)

        # COLD FAN CARD
        cfy = hfy + 200
        draw.rounded_rectangle([px + 20, cfy, px + PHONE_W - 20, cfy + 175], radius=18, fill=CARD_BG, outline=CARD_BORDER)
        draw.text((px + 35, cfy + 14), "COLD CHAMBER FAN (5010 BLOWER)", fill=COLOR_TEXT_MAIN, font=font_body_b)
        draw.text((px + 35, cfy + 34), "Chamber Destratification Mixer", fill=COLOR_TEXT_MUTED, font=font_small)
        
        # 3 Modes
        modes = ["AUTO DIFF", "ALWAYS ON", "FORCED OFF"]
        mw = (PHONE_W - 70 - 16) // 3
        for m_i, m_name in enumerate(modes):
            mx = px + 35 + m_i * (mw + 8)
            m_bg = (20, 45, 70) if m_i == 0 else CARD_BG
            m_bdr = COLOR_PRIMARY if m_i == 0 else CARD_BORDER
            draw.rounded_rectangle([mx, cfy + 60, mx + mw, cfy + 88], radius=8, fill=m_bg, outline=m_bdr)
            draw.text((mx + 6, cfy + 68), m_name, fill=COLOR_PRIMARY if m_i == 0 else COLOR_TEXT_MUTED, font=font_small_b)

        draw.text((px + 35, cfy + 105), "Auto logic: Starts if Top - Bot > 1.2C", fill=COLOR_TEXT_MUTED, font=font_small)
        draw.text((px + 35, cfy + 125), "Current Status: ACTIVE (Delta 0.4C)", fill=COLOR_SAFE, font=font_small_b)
        draw.text((px + 35, cfy + 145), "Motor Heat Loss: ~0.8W (Safe)", fill=COLOR_PRIMARY, font=font_small)

        # Lock button
        draw.rounded_rectangle([px + 20, cfy + 190, px + PHONE_W - 20, cfy + 230], radius=12, fill=CARD_BG, outline=CARD_BORDER)
        draw.text((px + 35, cfy + 202), "Revert All to Automatic Smart Governor", fill=COLOR_TEXT_MAIN, font=font_small_b)

    # SCREEN 4: 100% TURBO OVERDRIVE (Super Advanced!)
    elif idx == 3:
        draw.text((px + 20, py + 48), "Super Advanced Mode", fill=COLOR_TURBO, font=font_h2)
        draw.text((px + 20, py + 74), "Emergency rapid cooldown governor", fill=COLOR_TEXT_MUTED, font=font_small)

        # Big Glowing Turbo Card
        ty = py + 105
        draw.rounded_rectangle([px + 20, ty, px + PHONE_W - 20, ty + 215], radius=20, fill=(35, 18, 30), outline=COLOR_TURBO, width=2)
        draw.text((px + 35, ty + 16), "⚡ 100% TURBO PULL-DOWN", fill=COLOR_TURBO, font=font_h1)
        draw.text((px + 35, ty + 46), "Pushes TEC1-12706 to ~55W maximum power.", fill=COLOR_TEXT_MAIN, font=font_small)
        draw.text((px + 35, ty + 64), "Bypasses standard 75% battery / 47% DPM cap.", fill=COLOR_TEXT_MUTED, font=font_small)
        
        # Big Toggle Switch
        draw.rounded_rectangle([px + 35, ty + 95, px + PHONE_W - 35, ty + 145], radius=14, fill=(55, 22, 40), outline=COLOR_TURBO)
        draw.text((px + 55, ty + 110), "ENGAGE 100% OVERDRIVE", fill=(255, 255, 255), font=font_body_b)
        draw.rounded_rectangle([px + PHONE_W - 85, ty + 105, px + PHONE_W - 45, ty + 135], radius=15, fill=COLOR_TURBO)
        draw.ellipse([px + PHONE_W - 63, ty + 108, px + PHONE_W - 47, ty + 132], fill=(255, 255, 255))

        draw.text((px + 35, ty + 160), "Current State: ACTIVE (255/255 PWM)", fill=COLOR_TURBO, font=font_body_b)
        draw.text((px + 35, ty + 185), "Cooling Rate: 1.8 C / minute", fill=COLOR_SAFE, font=font_small_b)

        # Safety Guardrails Card
        sy = ty + 230
        draw.rounded_rectangle([px + 20, sy, px + PHONE_W - 20, sy + 175], radius=16, fill=CARD_BG, outline=CARD_BORDER)
        draw.text((px + 35, sy + 14), "AUTOMATIC SAFETY GUARDRAILS", fill=COLOR_WARN, font=font_small_b)
        
        rules = [
            ("Heatsink Cutoff:", "Auto-disengages if sink > 60 C"),
            ("Battery Floor:", "Disengages if pack < 11.0V (protects cells)"),
            ("Thermal Runaway:", "Aborts if delta T does not drop in 5m"),
            ("Hot Fan Force:", "Forces Hot Blower to 100% full blast")
        ]
        for r_i, (k, v) in enumerate(rules):
            ry = sy + 40 + r_i * 30
            draw.text((px + 35, ry), k, fill=COLOR_TEXT_MAIN, font=font_small_b)
            draw.text((px + 140, ry), v, fill=COLOR_TEXT_MUTED, font=font_small)

        # Warning Card
        wy = sy + 190
        draw.rounded_rectangle([px + 20, wy, px + PHONE_W - 20, wy + 55], radius=12, fill=(40, 30, 20), outline=COLOR_WARN)
        draw.text((px + 35, wy + 12), "CAUTION: Rapid heat build-up on heatsink.", fill=COLOR_WARN, font=font_small_b)
        draw.text((px + 35, wy + 30), "Use only during initial ambient cool-down.", fill=COLOR_TEXT_MUTED, font=font_small)

    # SCREEN 5: SETPOINTS & PRESETS (Tab 4)
    elif idx == 4:
        draw.text((px + 20, py + 48), "Cooling Target & Modes", fill=COLOR_TEXT_MAIN, font=font_h2)
        draw.text((px + 20, py + 74), "Adjust setpoint & tamper protection", fill=COLOR_TEXT_MUTED, font=font_small)

        # Big Dial
        dy = py + 105
        draw.rounded_rectangle([px + 20, dy, px + PHONE_W - 20, dy + 160], radius=18, fill=CARD_BG, outline=CARD_BORDER)
        draw.ellipse([px + 40, dy + 50, px + 88, dy + 98], fill=(30, 45, 75))
        draw.text((px + 58, dy + 58), "-", fill=COLOR_PRIMARY, font=font_h1)
        draw.text((px + 125, dy + 45), "4.0", fill=COLOR_TEXT_MAIN, font=font_large)
        draw.text((px + 205, dy + 55), "C", fill=COLOR_PRIMARY, font=font_h2)
        draw.ellipse([px + PHONE_W - 88, dy + 50, px + PHONE_W - 40, dy + 98], fill=(30, 45, 75))
        draw.text((px + PHONE_W - 70, dy + 58), "+", fill=COLOR_PRIMARY, font=font_h1)
        draw.text((px + 95, dy + 125), "Safe Range: 2.0C - 8.0C", fill=COLOR_SAFE, font=font_small)

        # Presets List
        py_list = dy + 180
        p_items = [
            ("Insulin Preset", "Human / Analog (4.0 C)", True),
            ("Vaccine Storage", "mRNA / Vector (5.0 C)", False),
            ("Biologics / Drops", "Monoclonal Antibodies (6.0 C)", False),
        ]
        for i, (title, desc, is_sel) in enumerate(p_items):
            iy = py_list + i * 68
            b_outline = COLOR_PRIMARY if is_sel else CARD_BORDER
            b_bg = (25, 42, 75) if is_sel else CARD_BG
            draw.rounded_rectangle([px + 20, iy, px + PHONE_W - 20, iy + 58], radius=14, fill=b_bg, outline=b_outline)
            draw.text((px + 36, iy + 12), title, fill=COLOR_TEXT_MAIN, font=font_body_b)
            draw.text((px + 36, iy + 32), desc, fill=COLOR_TEXT_MUTED, font=font_small)
            if is_sel:
                draw.text((px + PHONE_W - 55, iy + 20), "OK", fill=COLOR_PRIMARY, font=font_body_b)

        # Remote Lock
        ky = py_list + 220
        draw.rounded_rectangle([px + 20, ky, px + PHONE_W - 20, ky + 60], radius=14, fill=CARD_BG, outline=CARD_BORDER)
        draw.text((px + 36, ky + 12), "Lock Physical Buttons", fill=COLOR_TEXT_MAIN, font=font_body_b)
        draw.text((px + 36, ky + 32), "Prevents courier tampering", fill=COLOR_TEXT_MUTED, font=font_small)
        draw.rounded_rectangle([px + PHONE_W - 75, ky + 18, px + PHONE_W - 35, ky + 42], radius=12, fill=COLOR_SAFE)
        draw.ellipse([px + PHONE_W - 53, ky + 20, px + PHONE_W - 37, ky + 40], fill=(255, 255, 255))

    # SCREEN 6: AUDIT & EXPORT (Tab 5)
    elif idx == 5:
        draw.text((px + 20, py + 48), "Trip Compliance Audit", fill=COLOR_TEXT_MAIN, font=font_h2)
        draw.text((px + 20, py + 74), "Certified WHO PQS E003 report", fill=COLOR_TEXT_MUTED, font=font_small)

        # 24H Graph
        gy = py + 105
        draw.rounded_rectangle([px + 20, gy, px + PHONE_W - 20, gy + 195], radius=18, fill=CARD_BG, outline=CARD_BORDER)
        draw.text((px + 32, gy + 14), "24-HOUR THERMAL PROFILE", fill=COLOR_TEXT_MUTED, font=font_small_b)
        draw.line([px + 32, gy + 55, px + PHONE_W - 32, gy + 55], fill=(80, 30, 40), width=1)
        draw.text((px + PHONE_W - 65, gy + 44), "8.0C Max", fill=COLOR_ALERT, font=font_small)
        draw.line([px + 32, gy + 140, px + PHONE_W - 32, gy + 140], fill=(20, 50, 70), width=1)
        draw.text((px + PHONE_W - 65, gy + 145), "2.0C Min", fill=COLOR_PRIMARY, font=font_small)

        pts = []
        for x_step in range(0, PHONE_W - 64, 8):
            val_y = gy + 100 + int(8 * math.sin(x_step * 0.08))
            pts.append((px + 32 + x_step, val_y))
        draw.line(pts, fill=COLOR_SAFE, width=3)
        draw.text((px + 35, gy + 170), "Avg: 4.1C  ·  Excursions: 0 (100% Pass)", fill=COLOR_SAFE, font=font_small_b)

        # Metrics
        my = gy + 210
        draw.rounded_rectangle([px + 20, my, px + PHONE_W - 20, my + 115], radius=16, fill=CARD_BG, outline=CARD_BORDER)
        draw.text((px + 32, my + 14), "Mean Kinetic Temp (MKT):", fill=COLOR_TEXT_MUTED, font=font_small)
        draw.text((px + PHONE_W - 85, my + 12), "4.15C", fill=COLOR_TEXT_MAIN, font=font_body_b)
        draw.text((px + 32, my + 38), "Min / Max Recorded:", fill=COLOR_TEXT_MUTED, font=font_small)
        draw.text((px + PHONE_W - 120, my + 36), "3.8C / 4.6C", fill=COLOR_TEXT_MAIN, font=font_body_b)
        draw.text((px + 32, my + 62), "Monitored Duration:", fill=COLOR_TEXT_MUTED, font=font_small)
        draw.text((px + PHONE_W - 95, my + 60), "14h 22m", fill=COLOR_TEXT_MAIN, font=font_body_b)
        draw.text((px + 32, my + 86), "Compliance Standard:", fill=COLOR_TEXT_MUTED, font=font_small)
        draw.text((px + PHONE_W - 135, my + 84), "WHO PQS E003", fill=COLOR_SAFE, font=font_small_b)

        # Export Button
        ey = my + 130
        draw.rounded_rectangle([px + 20, ey, px + PHONE_W - 20, ey + 48], radius=14, fill=COLOR_PRIMARY)
        draw.text((px + 75, ey + 15), "EXPORT CERTIFIED PDF", fill=(10, 20, 40), font=font_body_b)

output_path = "doc/figma_medicool_ui_screens.png"
canvas.save(output_path, "PNG", dpi=(200, 200))
print(f"Updated Figma UI Mockup saved to {output_path}")

artifact_dir = r"C:\Users\Pranay K Pradeep\.gemini\antigravity\brain\fa9c2025-3022-49e8-ab3b-9e153cc88964"
artifact_out = os.path.join(artifact_dir, "figma_medicool_ui_screens.png")
canvas.save(artifact_out, "PNG")
print(f"Artifact copy saved to {artifact_out}")
