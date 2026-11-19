import matplotlib.pyplot as plt
import matplotlib.patches as patches

def draw_circuit_schematic(save_path):
    fig, ax = plt.subplots(figsize=(14, 8), dpi=200)
    ax.set_xlim(0, 14)
    ax.set_ylim(0, 8)
    ax.axis('off')
    
    # Title
    ax.text(7, 7.6, "MediCool – Actuator Driver Schematics", 
            fontsize=18, fontweight='bold', ha='center', va='center', color='#1A365D')
    ax.text(7, 7.2, "Peltier Low-Side Buck LC Filter & Blower Motor Low-Side Flyback Driver", 
            fontsize=11, style='italic', ha='center', va='center', color='#4A5568')

    # Background cards
    card_peltier = patches.FancyBboxPatch((0.5, 0.4), 6.2, 6.4, boxstyle="round,pad=0.1,rounding_size=0.15",
                                          ec="#2B6CB0", fc="#F7FAFC", lw=2, zorder=0)
    card_motor = patches.FancyBboxPatch((7.3, 0.4), 6.2, 6.4, boxstyle="round,pad=0.1,rounding_size=0.15",
                                        ec="#C53030", fc="#F7FAFC", lw=2, zorder=0)
    ax.add_patch(card_peltier)
    ax.add_patch(card_motor)

    ax.text(3.6, 6.4, "1. Peltier TEC1-12706 Driver (LC Filter)", fontsize=13, fontweight='bold', ha='center', color='#2B6CB0')
    ax.text(10.4, 6.4, "2. Blower Fan Driver (Hot / Cold)", fontsize=13, fontweight='bold', ha='center', color='#C53030')

    # ==================== PELTIER CIRCUIT ====================
    # 12V rail
    ax.plot([1.2, 6.0], [5.8, 5.8], color="#C53030", lw=3)
    ax.text(1.2, 6.0, "+12V Bus", fontsize=10, fontweight='bold', color="#C53030", va='bottom')

    # Peltier block
    p_box = patches.Rectangle((2.0, 4.4), 1.2, 0.9, ec="#2B6CB0", fc="#EBF8FF", lw=2, zorder=2)
    ax.add_patch(p_box)
    ax.text(2.6, 4.95, "Peltier\n(TEC1-12706)", fontsize=9, fontweight='bold', ha='center', va='center', color="#2B6CB0")
    ax.text(2.6, 5.4, "(+) Red", fontsize=8, color="#C53030", ha='center')
    ax.text(2.6, 4.25, "(–) Black", fontsize=8, color="#2D3748", ha='center')
    ax.plot([2.6, 2.6], [5.8, 5.3], color="#C53030", lw=2)

    # Capacitor across Peltier
    ax.plot([4.0, 4.0], [5.8, 4.2], color="#2D3748", lw=1.5)
    ax.plot([2.6, 4.0], [4.2, 4.2], color="#2D3748", lw=1.5)
    c_box = patches.Rectangle((3.6, 4.7), 0.8, 0.5, ec="#4A5568", fc="#EDF2F7", lw=1.5, zorder=2)
    ax.add_patch(c_box)
    ax.text(4.0, 4.95, "470µF\n25V", fontsize=8, fontweight='bold', ha='center', va='center')
    ax.text(4.0, 5.4, "+", fontsize=10, fontweight='bold', color="#C53030", ha='center')

    # Toroidal Inductor below Peltier
    ax.plot([2.6, 2.6], [4.2, 3.8], color="#2D3748", lw=2)
    l_box = patches.Rectangle((2.1, 3.3), 1.0, 0.5, ec="#D69E2E", fc="#FEFCBF", lw=2, zorder=2)
    ax.add_patch(l_box)
    ax.text(2.6, 3.55, "Inductor L1\n47µH–100µH (10A)", fontsize=8, fontweight='bold', ha='center', va='center', color="#744210")
    ax.plot([2.6, 2.6], [3.3, 2.7], color="#2D3748", lw=2)

    # Diode from Drain up to +12V
    ax.plot([5.2, 5.2], [2.7, 5.8], color="#2D3748", lw=2)
    ax.plot([2.6, 5.2], [2.7, 2.7], color="#2D3748", lw=2) # drain connection
    # Diode symbol
    d_tri = patches.Polygon([[4.95, 4.0], [5.45, 4.0], [5.2, 4.4]], closed=True, ec="#B7791F", fc="#ECC94B", lw=1.5, zorder=2)
    ax.add_patch(d_tri)
    ax.plot([4.95, 5.45], [4.4, 4.4], color="#B7791F", lw=2.5, zorder=3) # cathode bar
    ax.text(5.5, 4.2, "MBR1045\nSchottky\n(Stripe at top)", fontsize=8, color="#744210", va='center')

    # MOSFET Q1
    m_box = patches.Rectangle((2.1, 1.8), 1.0, 0.9, ec="#2B6CB0", fc="#E2E8F0", lw=2, zorder=2)
    ax.add_patch(m_box)
    ax.text(2.6, 2.3, "IRLZ44N\n(Q1)", fontsize=9, fontweight='bold', ha='center', va='center')
    ax.text(2.6, 2.85, "Drain (Pin 2)", fontsize=8, ha='center', color="#2B6CB0")
    ax.text(1.7, 2.25, "Gate (1)", fontsize=8, ha='right', color="#2B6CB0")
    ax.text(2.6, 1.6, "Source (3)", fontsize=8, ha='center', color="#2B6CB0")
    ax.plot([2.6, 2.6], [1.8, 1.1], color="#2D3748", lw=2)
    # Ground symbol
    ax.plot([2.2, 3.0], [1.1, 1.1], color="#2D3748", lw=2)
    ax.plot([2.4, 2.8], [0.95, 0.95], color="#2D3748", lw=1.5)
    ax.plot([2.5, 2.7], [0.8, 0.8], color="#2D3748", lw=1)
    ax.text(3.1, 1.0, "GND", fontsize=8, fontweight='bold')

    # Gate network
    ax.plot([0.8, 2.1], [2.25, 2.25], color="#2B6CB0", lw=1.5)
    r1_box = patches.Rectangle((1.1, 2.1), 0.5, 0.3, ec="#4A5568", fc="#CBD5E0", lw=1, zorder=3)
    ax.add_patch(r1_box)
    ax.text(1.35, 2.25, "150Ω", fontsize=7.5, ha='center', va='center')
    ax.text(0.7, 2.25, "ESP32\nGPIO 25", fontsize=8, fontweight='bold', ha='right', va='center', color="#2B6CB0")

    # Pull-down 10k
    ax.plot([1.8, 1.8], [2.25, 1.3], color="#2D3748", lw=1.5)
    ax.plot([1.8, 2.6], [1.3, 1.3], color="#2D3748", lw=1.5)
    r2_box = patches.Rectangle((1.65, 1.5), 0.3, 0.5, ec="#4A5568", fc="#CBD5E0", lw=1, zorder=3)
    ax.add_patch(r2_box)
    ax.text(1.5, 1.75, "10kΩ", fontsize=7.5, ha='right', va='center')

    # ==================== MOTOR CIRCUIT ====================
    # 12V rail
    ax.plot([8.0, 12.8], [5.8, 5.8], color="#C53030", lw=3)
    ax.text(8.0, 6.0, "+12V Bus", fontsize=10, fontweight='bold', color="#C53030", va='bottom')

    # Motor circle
    m_circle = patches.Circle((9.6, 4.6), 0.55, ec="#C53030", fc="#FFF5F5", lw=2, zorder=2)
    ax.add_patch(m_circle)
    ax.text(9.6, 4.6, "FAN\n(M)", fontsize=10, fontweight='bold', ha='center', va='center', color="#C53030")
    ax.plot([9.6, 9.6], [5.8, 5.15], color="#C53030", lw=2)
    ax.text(9.6, 5.35, "(+) Red", fontsize=8, color="#C53030", ha='center')
    ax.text(9.6, 3.85, "(–) Black", fontsize=8, color="#2D3748", ha='center')

    # Flyback diode in parallel with motor
    ax.plot([11.2, 11.2], [3.9, 5.8], color="#2D3748", lw=2)
    ax.plot([9.6, 11.2], [3.9, 3.9], color="#2D3748", lw=2)
    # Diode symbol
    d2_tri = patches.Polygon([[10.95, 4.5], [11.45, 4.5], [11.2, 4.9]], closed=True, ec="#9B2C2C", fc="#FEB2B2", lw=1.5, zorder=2)
    ax.add_patch(d2_tri)
    ax.plot([10.95, 11.45], [4.9, 4.9], color="#9B2C2C", lw=2.5, zorder=3) # cathode bar
    ax.text(11.5, 4.7, "1N5819\nFlyback\n(Stripe at top)", fontsize=8, color="#9B2C2C", va='center')

    # MOSFET Q2/Q3
    ax.plot([9.6, 9.6], [3.9, 2.7], color="#2D3748", lw=2)
    m2_box = patches.Rectangle((9.1, 1.8), 1.0, 0.9, ec="#C53030", fc="#E2E8F0", lw=2, zorder=2)
    ax.add_patch(m2_box)
    ax.text(9.6, 2.3, "IRLZ44N\n(Q2 / Q3)", fontsize=9, fontweight='bold', ha='center', va='center')
    ax.text(9.6, 2.85, "Drain (Pin 2)", fontsize=8, ha='center', color="#C53030")
    ax.text(8.7, 2.25, "Gate (1)", fontsize=8, ha='right', color="#C53030")
    ax.text(9.6, 1.6, "Source (3)", fontsize=8, ha='center', color="#C53030")
    ax.plot([9.6, 9.6], [1.8, 1.1], color="#2D3748", lw=2)
    # Ground symbol
    ax.plot([9.2, 10.0], [1.1, 1.1], color="#2D3748", lw=2)
    ax.plot([9.4, 9.8], [0.95, 0.95], color="#2D3748", lw=1.5)
    ax.plot([9.5, 9.7], [0.8, 0.8], color="#2D3748", lw=1)
    ax.text(10.1, 1.0, "GND", fontsize=8, fontweight='bold')

    # Gate network
    ax.plot([7.8, 9.1], [2.25, 2.25], color="#C53030", lw=1.5)
    r3_box = patches.Rectangle((8.1, 2.1), 0.5, 0.3, ec="#4A5568", fc="#CBD5E0", lw=1, zorder=3)
    ax.add_patch(r3_box)
    ax.text(8.35, 2.25, "150Ω", fontsize=7.5, ha='center', va='center')
    ax.text(7.7, 2.25, "ESP32\nGPIO 26/27", fontsize=8, fontweight='bold', ha='right', va='center', color="#C53030")

    # Pull-down 10k
    ax.plot([8.8, 8.8], [2.25, 1.3], color="#2D3748", lw=1.5)
    ax.plot([8.8, 9.6], [1.3, 1.3], color="#2D3748", lw=1.5)
    r4_box = patches.Rectangle((8.65, 1.5), 0.3, 0.5, ec="#4A5568", fc="#CBD5E0", lw=1, zorder=3)
    ax.add_patch(r4_box)
    ax.text(8.5, 1.75, "10kΩ", fontsize=7.5, ha='right', va='center')

    plt.tight_layout()
    plt.savefig(save_path, dpi=200, bbox_inches='tight')
    plt.close()
    print(f"Actuator schematic saved to: {save_path}")

if __name__ == "__main__":
    draw_circuit_schematic("d:/pranay\clg/tinkerers lab/vishwakarma/images/peltier_and_fan_circuits.png")
    draw_circuit_schematic("d:/pranay/clg/tinkerers lab/vishwakarma/doc/peltier_and_fan_circuits.png")

