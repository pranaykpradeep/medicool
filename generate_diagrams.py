import matplotlib.pyplot as plt
import matplotlib.patches as patches

def draw_block_diagram(save_path):
    fig, ax = plt.subplots(figsize=(16, 11), dpi=200)
    ax.set_xlim(0, 16)
    ax.set_ylim(0, 11)
    ax.axis('off')
    
    # Title
    ax.text(8, 10.5, "MediCool – Master System Architecture & Power Flow", 
            fontsize=20, fontweight='bold', ha='center', va='center', color='#1A365D')
    ax.text(8, 10.1, "90W USB-C PD / 3S 10Ah Li-ion Dual Power Failover & Thermoelectric Regulation", 
            fontsize=12, style='italic', ha='center', va='center', color='#4A5568')

    def draw_box(x, y, w, h, title, subtitle="", color="#E2E8F0", border="#4A5568", text_color="#1A202C", corner_radius=0.15):
        box = patches.FancyBboxPatch((x, y), w, h, boxstyle=f"round,pad=0.08,rounding_size={corner_radius}",
                                     ec=border, fc=color, lw=2, zorder=2)
        ax.add_patch(box)
        if subtitle:
            ax.text(x + w/2, y + h*0.62, title, fontsize=11, fontweight='bold', ha='center', va='center', color=text_color, zorder=3)
            ax.text(x + w/2, y + h*0.30, subtitle, fontsize=8.5, ha='center', va='center', color=text_color, zorder=3)
        else:
            ax.text(x + w/2, y + h/2, title, fontsize=11, fontweight='bold', ha='center', va='center', color=text_color, zorder=3)

    def draw_arrow(x1, y1, x2, y2, label="", color="#2B6CB0", lw=2.5, style="->", rad=0, label_offset=(0,0)):
        connectionstyle = f"arc3,rad={rad}" if rad != 0 else "arc3,rad=0"
        ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                    arrowprops=dict(arrowstyle=style, color=color, lw=lw, connectionstyle=connectionstyle),
                    zorder=1)
        if label:
            mx, my = (x1 + x2)/2 + label_offset[0], (y1 + y2)/2 + label_offset[1]
            ax.text(mx, my, label, fontsize=9, fontweight='bold', color=color, ha='center', va='center',
                    bbox=dict(boxstyle="round,pad=0.2", fc="white", ec=color, lw=1, alpha=0.9), zorder=4)

    # 1. External Supply Stage
    draw_box(0.5, 8.2, 2.5, 1.2, "USB-C PD Adapter", "90W (20V / 4.5A)", "#BEE3F8", "#2B6CB0")
    draw_box(3.8, 8.2, 2.4, 1.2, "PD Decoy Trigger", "Item 21 (20V Mode)", "#BEE3F8", "#2B6CB0")
    draw_arrow(3.0, 8.8, 3.8, 8.8, "Type-C")

    # 2. 20V Distribution
    # Branch 1 -> 300W Buck Converter (Steps down 20V to 12.0V)
    draw_box(7.2, 8.2, 2.8, 1.2, "300W Buck Converter", "20V In -> 12.0V Out (Item 16)", "#C6F6D5", "#276749")
    draw_arrow(6.2, 8.8, 7.2, 8.8, "20V DC")

    # Branch 2 -> XL4015 3S Charger
    draw_box(7.2, 6.2, 2.8, 1.2, "XL4015 3S Charger", "CC/CV 12.6V @ 2.5A (Item 17)", "#FEFCBF", "#B7791F")
    ax.annotate('', xy=(7.2, 6.8), xytext=(6.5, 8.8),
                arrowprops=dict(arrowstyle="->", color="#B7791F", lw=2, connectionstyle="arc3,rad=-0.15"), zorder=1)
    ax.text(6.4, 7.6, "20V DC", fontsize=8.5, fontweight='bold', color="#B7791F", bbox=dict(boxstyle="round,pad=0.15", fc="white", ec="#B7791F", lw=0.8))

    # Anti-backfeed Schottky Diode
    draw_box(10.5, 6.2, 1.8, 1.2, "Schottky Diode", "MBR1045 / SS54\n(Anti-Backfeed)", "#FEFCBF", "#B7791F")
    draw_arrow(10.0, 6.8, 10.5, 6.8)

    # 3. Battery Subsystem
    draw_box(12.8, 6.2, 2.6, 1.2, "3S4P 18650 Battery", "11.1V Nom / 10Ah / 111Wh", "#E9D8FD", "#6B46C1")
    draw_arrow(12.3, 6.8, 12.8, 6.8, "12.6V Chg")

    draw_box(12.8, 8.2, 2.6, 1.0, "10A Inline Fuse", "Item 18 (Circuit Protection)", "#FED7D7", "#C53030")
    draw_arrow(14.1, 7.4, 14.1, 8.2, "Bat Power")

    # 4. Automatic Relay Changeover
    draw_box(10.5, 8.0, 1.8, 1.5, "12V SPDT Relay", "Failover Switch (Item 14)\nNO: Ext 12V | NC: Bat", "#ED8936", "#C05621", text_color="#FFFFFF")
    draw_arrow(10.0, 8.8, 10.5, 8.8, "NO (Ext)")
    draw_arrow(12.8, 8.7, 12.3, 8.7, "NC (Bat)")

    # Relay Coil excitation from 12V Ext
    ax.annotate('', xy=(11.4, 9.5), xytext=(8.6, 9.4),
                arrowprops=dict(arrowstyle="->", color="#C05621", lw=1.5, ls="--", connectionstyle="arc3,rad=-0.2"), zorder=1)
    ax.text(10.0, 9.8, "Coil 12V (Energized when Ext ON)\n+ 1N4007 Flyback Diode", fontsize=8, color="#C05621", ha='center', bbox=dict(boxstyle="round,pad=0.15", fc="#FFF5F5", ec="#C05621", lw=0.8))

    # 5. 12V Main System Bus
    draw_box(5.0, 4.4, 6.0, 1.0, "12V MAIN SYSTEM BUS", "Buffered by 2200uF / 25V Capacitor (Seamless 15ms Transfer)", "#ED8936", "#9C4221", text_color="#FFFFFF", corner_radius=0.1)
    draw_arrow(11.4, 8.0, 8.0, 5.4, "Relay COM (Pin 30)", color="#9C4221", lw=3)

    # 6. Branch from 12V Bus -> 5V Buck -> ESP32 & User Interface
    draw_box(0.5, 4.4, 2.4, 1.0, "12V -> 5V Buck", "Item 15 (Logic Power)", "#C6F6D5", "#276749")
    draw_arrow(5.0, 4.9, 2.9, 4.9, "12V Bus")

    draw_box(0.5, 1.2, 3.2, 2.4, "ESP32 Master Controller", "Item 1 (Dual Core MCU)\n• GPIO 25: Peltier PWM\n• GPIO 26: Hot Fan PWM\n• GPIO 27: Cold Fan PWM\n• GPIO 4: 1-Wire Temp Bus\n• GPIO 21/22: I2C OLED", "#E2E8F0", "#2D3748")
    draw_arrow(1.7, 4.4, 1.7, 3.6, "5V DC")

    # OLED & Buttons
    draw_box(0.5, 0.1, 1.5, 0.8, "0.96\" OLED", "I2C (SSD1306)", "#CBD5E0", "#4A5568")
    draw_box(2.2, 0.1, 1.5, 0.8, "3x Buttons", "Up / Down / Mode", "#CBD5E0", "#4A5568")
    draw_arrow(1.2, 1.2, 1.2, 0.9)
    draw_arrow(2.9, 1.2, 2.9, 0.9)

    # 7. Actuator Loops
    # A. Peltier Loop with LC Filter
    draw_box(4.5, 1.8, 3.2, 1.8, "Peltier Cooling Loop", "TEC1-12706 (Item 3)\n+ LC Filter (47uH + 470uF)\n+ MBR1045 Schottky\nDriven by IRLZ44N #1", "#EBF8FF", "#3182CE")
    draw_arrow(6.1, 4.4, 6.1, 3.6, "12V Power")
    # ESP32 to Peltier Gate
    draw_arrow(3.7, 2.7, 4.5, 2.7, "PWM (GPIO 25)", color="#3182CE", lw=1.8)

    # B. 7530 Hot Blower Fan
    draw_box(8.2, 1.8, 3.2, 1.8, "7530 Hot Blower Fan", "External Heatsink (Item 5)\n+ 1N5819 Flyback Diode\nProportional Temp Speed\nDriven by IRLZ44N #2", "#FFF5F5", "#E53E3E")
    draw_arrow(8.5, 4.4, 8.5, 3.6, "12V Power")
    # ESP32 to Hot Fan Gate
    ax.annotate('', xy=(8.2, 2.4), xytext=(3.7, 2.4),
                arrowprops=dict(arrowstyle="->", color="#E53E3E", lw=1.8, connectionstyle="arc3,rad=-0.1"), zorder=1)
    ax.text(5.95, 1.15, "PWM (GPIO 26)", fontsize=8, color="#E53E3E", ha='center', bbox=dict(boxstyle="round,pad=0.15", fc="white", ec="#E53E3E", lw=0.8))

    # C. 5010 Cold Blower Fan
    draw_box(11.9, 1.8, 3.5, 1.8, "5010 Cold Blower Fan", "Internal Circulation (Item 4)\n+ 1N5819 Flyback Diode\nDynamic Destratification\nDriven by IRLZ44N #3", "#F0FFF4", "#38A169")
    draw_arrow(10.5, 4.4, 12.5, 3.6, "12V Power")
    # ESP32 to Cold Fan Gate
    ax.annotate('', xy=(11.9, 2.1), xytext=(3.7, 2.1),
                arrowprops=dict(arrowstyle="->", color="#38A169", lw=1.8, connectionstyle="arc3,rad=-0.18"), zorder=1)
    ax.text(9.5, 0.7, "PWM (GPIO 27)", fontsize=8, color="#38A169", ha='center', bbox=dict(boxstyle="round,pad=0.15", fc="white", ec="#38A169", lw=0.8))

    # 8. 3-Sensor 1-Wire Digital Bus
    draw_box(11.9, 4.2, 3.5, 1.4, "3x DS18B20 Probes", "1-Wire Digital Bus (GPIO 4)\n• T1: Chamber Top\n• T2: Chamber Bottom\n• T3: Hot Heatsink", "#FAF5FF", "#805AD5")
    ax.annotate('', xy=(3.7, 3.0), xytext=(11.9, 4.9),
                arrowprops=dict(arrowstyle="<-", color="#805AD5", lw=2, connectionstyle="arc3,rad=0.15"), zorder=1)
    ax.text(7.8, 3.8, "1-Wire Bus (GPIO 4 with 4.7k Pullup)", fontsize=8.5, fontweight='bold', color="#805AD5", ha='center', bbox=dict(boxstyle="round,pad=0.15", fc="white", ec="#805AD5", lw=0.8))

    plt.tight_layout()
    plt.savefig(save_path, dpi=200, bbox_inches='tight')
    plt.close()
    print(f"Diagram successfully saved to: {save_path}")

if __name__ == "__main__":
    draw_block_diagram("d:/pranay/clg/tinkerers lab/vishwakarma/images/system_architecture_block_diagram.png")
    draw_block_diagram("d:/pranay/clg/tinkerers lab/vishwakarma/doc/system_architecture_block_diagram.png")

