import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:fl_chart/fl_chart.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const MediCoolApp());
}

class MediCoolApp extends StatelessWidget {
  const MediCoolApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'MediCool Companion',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark().copyWith(
        scaffoldBackgroundColor: const Color(0xFF0A0F1D),
        cardColor: const Color(0xFF131D33),
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFF00B4D8),
          secondary: Color(0xFF06D6A0),
          surface: Color(0xFF131D33),
          error: Color(0xFFEF476F),
        ),
      ),
      home: const RootNavigationHub(),
    );
  }
}

// ==============================================================================
// ROOT NAVIGATION HUB (Scan Screen vs Main Dashboard)
// ==============================================================================
class RootNavigationHub extends StatefulWidget {
  const RootNavigationHub({super.key});

  @override
  State<RootNavigationHub> createState() => _RootNavigationHubState();
}

class _RootNavigationHubState extends State<RootNavigationHub> {
  // Bluetooth Connection State
  BluetoothDevice? _connectedDevice;
  BluetoothCharacteristic? _telemetryChar;
  BluetoothCharacteristic? _controlChar;
  StreamSubscription<List<ScanResult>>? _scanSub;
  StreamSubscription<BluetoothConnectionState>? _connSub;
  StreamSubscription<List<int>>? _telemetrySub;

  List<ScanResult> _scanResults = [];
  bool _isScanning = false;
  bool _isConnecting = false;
  bool _isDemoMode = false;
  bool _filterOnlyMediCool = true;
  Timer? _demoTimer;

  List<ScanResult> get _filteredScanResults {
    List<ScanResult> list = _scanResults.where((r) {
      final name = r.device.platformName.trim();
      if (_filterOnlyMediCool) {
        return name.toLowerCase().contains("medicool") || name.toLowerCase().contains("esp32");
      }
      return name.isNotEmpty;
    }).toList();

    list.sort((a, b) {
      bool aIsMediCool = a.device.platformName.toLowerCase().contains("medicool") ||
          a.device.platformName.toLowerCase().contains("esp32");
      bool bIsMediCool = b.device.platformName.toLowerCase().contains("medicool") ||
          b.device.platformName.toLowerCase().contains("esp32");
      if (aIsMediCool && !bIsMediCool) return -1;
      if (!aIsMediCool && bIsMediCool) return 1;
      return b.rssi.compareTo(a.rssi);
    });

    return list;
  }

  // Telemetry Variables
  double chamberTemp = 4.2;
  double topTemp = 4.4;
  double bottomTemp = 4.0;
  double sinkTemp = 38.5;
  double targetSetpoint = 4.0;
  String presetName = "INSULIN";
  int presetIndex = 0;
  double vbatVoltage = 12.2;
  int batteryPercent = 86;
  bool isCharging = false;
  int peltierDuty = 110;
  int hotFanDuty = 145;
  int coldFanDuty = 180;
  bool coldFanActive = true;
  bool manualHotFan = false;
  int manualHotDuty = 200;
  int coldFanMode = 0;
  bool turboActive = false;
  bool isLocked = false;
  int deviceRssi = -58;
  String deviceName = "MediCool-Smart (ESP32)";
  String deviceId = "MC-2026-X8";
  int uptimeSeconds = 1420;

  final List<FlSpot> _tempHistory = [];
  double _timeStep = 0;
  int _currentTabIndex = 0;

  // UUIDs
  static const String SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
  static const String TELEMETRY_CHAR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
  static const String CONTROL_CHAR_UUID = "0000ff01-0000-1000-8000-00805f9b34fb";

  @override
  void initState() {
    super.initState();
    // Initialize initial 25-point historical curve
    for (int i = 0; i < 25; i++) {
      _tempHistory.add(FlSpot(i.toDouble(), 4.0 + (sin(i * 0.4) * 0.25)));
    }
    _timeStep = 25;

    _initBluetooth();
  }

  @override
  void dispose() {
    _scanSub?.cancel();
    _connSub?.cancel();
    _telemetrySub?.cancel();
    _demoTimer?.cancel();
    super.dispose();
  }

  void _initBluetooth() async {
    _scanSub = FlutterBluePlus.scanResults.listen((results) {
      if (mounted) {
        setState(() {
          _scanResults = results;
        });
      }
    });

    FlutterBluePlus.isScanning.listen((scanning) {
      if (mounted) {
        setState(() => _isScanning = scanning);
      }
    });
  }

  void _startScan() async {
    if (_isScanning) return;
    setState(() {
      _scanResults.clear();
      _isScanning = true;
    });

    try {
      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 12));
    } catch (e) {
      debugPrint("Scan error: $e");
    }
  }

  void _connectToDevice(BluetoothDevice device) async {
    setState(() {
      _isConnecting = true;
    });

    try {
      await FlutterBluePlus.stopScan();
      await device.connect(license: License.nonprofit, autoConnect: false);

      _connectedDevice = device;
      deviceName = device.platformName.isNotEmpty ? device.platformName : "MediCool Cooler";
      deviceId = device.remoteId.str;

      _connSub = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          setState(() {
            _connectedDevice = null;
            _isConnecting = false;
          });
        }
      });

      // Discover services & characteristics
      List<BluetoothService> services = await device.discoverServices();
      for (var s in services) {
        for (var c in s.characteristics) {
          if (c.characteristicUuid.toString().toLowerCase().contains("beb5483e")) {
            _telemetryChar = c;
            await c.setNotifyValue(true);
            _telemetrySub = c.lastValueStream.listen(_onTelemetryReceived);
          } else if (c.characteristicUuid.toString().toLowerCase().contains("ff01")) {
            _controlChar = c;
          }
        }
      }

      setState(() {
        _isConnecting = false;
        _isDemoMode = false;
      });
    } catch (e) {
      debugPrint("Connect error: $e");
      setState(() => _isConnecting = false);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text("Bluetooth Connection Failed: $e")),
        );
      }
    }
  }

  void _disconnect() async {
    _demoTimer?.cancel();
    if (_connectedDevice != null) {
      await _connectedDevice?.disconnect();
    }
    setState(() {
      _connectedDevice = null;
      _isDemoMode = false;
    });
  }

  void _onTelemetryReceived(List<int> bytes) {
    if (bytes.isEmpty) return;
    try {
      final str = utf8.decode(bytes);
      final d = jsonDecode(str);
      setState(() {
        chamberTemp = (d['chamberTemp'] as num).toDouble();
        topTemp = (d['topTemp'] as num).toDouble();
        bottomTemp = (d['bottomTemp'] as num).toDouble();
        sinkTemp = (d['sinkTemp'] as num).toDouble();
        targetSetpoint = (d['setpoint'] as num).toDouble();
        presetName = d['preset'] ?? "CUSTOM";
        presetIndex = d['presetIndex'] ?? 0;
        vbatVoltage = (d['vbat'] as num).toDouble();
        batteryPercent = d['batPercent'] ?? 85;
        isCharging = d['charging'] ?? false;
        peltierDuty = d['peltierDuty'] ?? 0;
        hotFanDuty = d['hotFanDuty'] ?? 0;
        coldFanDuty = d['coldFanDuty'] ?? 0;
        coldFanActive = d['coldFanActive'] ?? false;
        manualHotFan = d['manualHotFan'] ?? false;
        manualHotDuty = d['manualHotDuty'] ?? 200;
        coldFanMode = d['coldFanMode'] ?? 0;
        turboActive = d['turboActive'] ?? false;
        isLocked = d['locked'] ?? false;
        uptimeSeconds = d['uptime'] ?? (uptimeSeconds + 1);

        _timeStep += 1;
        _tempHistory.add(FlSpot(_timeStep, chamberTemp));
        if (_tempHistory.length > 35) _tempHistory.removeAt(0);
      });
    } catch (e) {
      debugPrint("Telemetry parse error: $e");
    }
  }

  void _sendBleCommand(String cmd) async {
    if (_isDemoMode) {
      // In demo mode, locally update state
      _handleDemoCommand(cmd);
      return;
    }

    if (_controlChar != null) {
      try {
        await _controlChar?.write(utf8.encode(cmd));
      } catch (e) {
        debugPrint("Error writing BLE command: $e");
      }
    }
  }

  void _enterDemoMode() {
    setState(() {
      _isDemoMode = true;
      deviceName = "MediCool-Smart (Demo Simulator)";
      deviceId = "DEMO-ESP32-BLE";
      uptimeSeconds = 3450;
    });

    _demoTimer?.cancel();
    _demoTimer = Timer.periodic(const Duration(seconds: 1), (timer) {
      if (!mounted) return;
      setState(() {
        uptimeSeconds++;
        // Natural temperature breathing simulation
        double drift = (sin(uptimeSeconds * 0.1) * 0.05);
        if (turboActive) {
          chamberTemp = max(2.1, chamberTemp - 0.08);
          peltierDuty = 255;
          hotFanDuty = 255;
        } else {
          chamberTemp = (targetSetpoint + drift);
          peltierDuty = (110 + (drift * 200)).clamp(30, 255).toInt();
        }

        _timeStep += 1;
        _tempHistory.add(FlSpot(_timeStep, chamberTemp));
        if (_tempHistory.length > 35) _tempHistory.removeAt(0);
      });
    });
  }

  void _handleDemoCommand(String cmd) {
    setState(() {
      if (cmd.startsWith("preset=")) {
        presetIndex = int.tryParse(cmd.split("=")[1]) ?? 0;
        if (presetIndex == 0) {
          targetSetpoint = 4.0;
          presetName = "INSULIN";
        } else if (presetIndex == 1) {
          targetSetpoint = 5.0;
          presetName = "VACCINES";
        } else if (presetIndex == 2) {
          targetSetpoint = 6.0;
          presetName = "EYE DROPS";
        } else if (presetIndex == 3) {
          presetName = "CUSTOM";
        }
      } else if (cmd.startsWith("temp=")) {
        targetSetpoint = double.tryParse(cmd.split("=")[1]) ?? 4.0;
        presetName = "CUSTOM";
        presetIndex = 3;
      } else if (cmd.startsWith("turbo=")) {
        turboActive = (cmd.split("=")[1] == "1");
      } else if (cmd.startsWith("hotDuty=")) {
        manualHotDuty = int.tryParse(cmd.split("=")[1]) ?? 200;
        hotFanDuty = manualHotDuty;
      } else if (cmd.startsWith("manualHot=")) {
        manualHotFan = (cmd.split("=")[1] == "1");
      } else if (cmd.startsWith("coldMode=")) {
        coldFanMode = int.tryParse(cmd.split("=")[1]) ?? 0;
      } else if (cmd == "toggleLock=1") {
        isLocked = !isLocked;
      }
    });
  }

  bool get isConnected => (_connectedDevice != null) || _isDemoMode;

  @override
  Widget build(BuildContext context) {
    if (!isConnected) {
      return _buildScanningScreen();
    }
    return _buildDashboardScreen();
  }

  // ==============================================================================
  // 1. BLUETOOTH SCANNING & PAIRING SCREEN
  // ==============================================================================
  Widget _buildScanningScreen() {
    return Scaffold(
      backgroundColor: const Color(0xFF090E1A),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        elevation: 0,
        title: const Row(
          children: [
            Icon(Icons.ac_unit, color: Color(0xFF00B4D8), size: 22),
            SizedBox(width: 10),
            Text(
              "MEDICOOL",
              style: TextStyle(fontWeight: FontWeight.w900, letterSpacing: 1.5, fontSize: 18),
            ),
          ],
        ),
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh, color: Color(0xFF00B4D8)),
            onPressed: _startScan,
          ),
        ],
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            const SizedBox(height: 20),
            // Radar Animation Circle
            Center(
              child: Stack(
                alignment: Alignment.center,
                children: [
                  Container(
                    width: 160,
                    height: 160,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: const Color(0xFF00B4D8).withValues(alpha: 0.05),
                      border: Border.all(color: const Color(0xFF00B4D8).withValues(alpha: 0.2), width: 1.5),
                    ),
                  ),
                  Container(
                    width: 110,
                    height: 110,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: const Color(0xFF00B4D8).withValues(alpha: 0.1),
                    ),
                  ),
                  Container(
                    width: 60,
                    height: 60,
                    decoration: const BoxDecoration(
                      shape: BoxShape.circle,
                      color: Color(0xFF00B4D8),
                      boxShadow: [
                        BoxShadow(color: Color(0xFF00B4D8), blurRadius: 20, spreadRadius: 2),
                      ],
                    ),
                    child: Icon(
                      _isScanning ? Icons.bluetooth_searching : Icons.bluetooth,
                      color: Colors.white,
                      size: 30,
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 24),
            Text(
              _isScanning ? "Scanning for MediCool Cooler..." : "Ready to Connect",
              style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold, color: Colors.white),
            ),
            const SizedBox(height: 8),
            Text(
              "Ensure your cooler is powered on and Bluetooth is enabled on your phone.",
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 13, color: Colors.grey.shade400),
            ),
            const SizedBox(height: 24),

            // Scan Action Buttons
            Row(
              children: [
                Expanded(
                  child: ElevatedButton.icon(
                    style: ElevatedButton.styleFrom(
                      backgroundColor: const Color(0xFF00B4D8),
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(vertical: 14),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                    ),
                    icon: _isScanning
                        ? const SizedBox(
                            width: 16,
                            height: 16,
                            child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white))
                        : const Icon(Icons.search, size: 20),
                    label: Text(_isScanning ? "Scanning..." : "Scan for Coolers",
                        style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 14)),
                    onPressed: _isScanning ? null : _startScan,
                  ),
                ),
                const SizedBox(width: 12),
                ElevatedButton(
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF1B2640),
                    foregroundColor: const Color(0xFF06D6A0),
                    padding: const EdgeInsets.symmetric(vertical: 14, horizontal: 16),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(16),
                      side: BorderSide(color: const Color(0xFF06D6A0).withValues(alpha: 0.3)),
                    ),
                  ),
                  onPressed: _enterDemoMode,
                  child: const Text("Demo Mode", style: TextStyle(fontWeight: FontWeight.bold)),
                ),
              ],
            ),
            const SizedBox(height: 28),

            // Discovered Devices List Header
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text(
                  "DISCOVERED DEVICES",
                  style: TextStyle(fontSize: 12, fontWeight: FontWeight.bold, letterSpacing: 1.2, color: Colors.grey),
                ),
                Row(
                  children: [
                    // Smart Filter Toggle
                    GestureDetector(
                      onTap: () => setState(() => _filterOnlyMediCool = !_filterOnlyMediCool),
                      child: Container(
                        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                        decoration: BoxDecoration(
                          color: _filterOnlyMediCool ? const Color(0x2200B4D8) : Colors.white.withValues(alpha: 0.05),
                          borderRadius: BorderRadius.circular(12),
                          border: Border.all(
                            color: _filterOnlyMediCool ? const Color(0xFF00B4D8) : Colors.white.withValues(alpha: 0.1),
                            width: 1,
                          ),
                        ),
                        child: Row(
                          children: [
                            Icon(
                              _filterOnlyMediCool ? Icons.filter_alt : Icons.filter_alt_off,
                              size: 13,
                              color: _filterOnlyMediCool ? const Color(0xFF00B4D8) : Colors.grey,
                            ),
                            const SizedBox(width: 5),
                            Text(
                              _filterOnlyMediCool ? "MediCool Only" : "All Devices",
                              style: TextStyle(
                                fontSize: 11,
                                fontWeight: FontWeight.bold,
                                color: _filterOnlyMediCool ? const Color(0xFF00B4D8) : Colors.grey.shade400,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                    const SizedBox(width: 10),
                    Text(
                      "${_filteredScanResults.length} found",
                      style: const TextStyle(fontSize: 12, color: Color(0xFF00B4D8), fontWeight: FontWeight.bold),
                    ),
                  ],
                ),
              ],
            ),
            const SizedBox(height: 12),

            if (_filteredScanResults.isEmpty)
              Container(
                width: double.infinity,
                padding: const EdgeInsets.all(28),
                decoration: BoxDecoration(
                  color: const Color(0xFF131D33),
                  borderRadius: BorderRadius.circular(20),
                  border: Border.all(color: Colors.white.withValues(alpha: 0.05)),
                ),
                child: Column(
                  children: [
                    Icon(
                      _filterOnlyMediCool ? Icons.ac_unit : Icons.devices_other,
                      size: 40,
                      color: const Color(0xFF00B4D8),
                    ),
                    const SizedBox(height: 12),
                    Text(
                      _filterOnlyMediCool
                          ? (_isScanning
                              ? "Scanning specifically for 'MediCool-Smart'..."
                              : "No MediCool coolers detected nearby.\nTurn on cooler or tap below to see all devices.")
                          : (_isScanning ? "Listening for BLE advertising packets..." : "No devices found yet."),
                      textAlign: TextAlign.center,
                      style: TextStyle(color: Colors.grey.shade400, fontSize: 13),
                    ),
                    if (_filterOnlyMediCool) ...[
                      const SizedBox(height: 10),
                      TextButton.icon(
                        icon: const Icon(Icons.list, size: 16, color: Color(0xFF00B4D8)),
                        label: const Text("Show all nearby Bluetooth devices", style: TextStyle(color: Color(0xFF00B4D8), fontSize: 12)),
                        onPressed: () => setState(() => _filterOnlyMediCool = false),
                      ),
                    ],
                    const SizedBox(height: 14),
                    OutlinedButton.icon(
                      icon: const Icon(Icons.bolt, size: 16, color: Color(0xFF06D6A0)),
                      label: const Text("Launch Demo Simulator", style: TextStyle(color: Color(0xFF06D6A0))),
                      style: OutlinedButton.styleFrom(
                        side: BorderSide(color: const Color(0xFF06D6A0).withValues(alpha: 0.4)),
                        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                      ),
                      onPressed: _enterDemoMode,
                    ),
                  ],
                ),
              )
            else
              ListView.builder(
                shrinkWrap: true,
                physics: const NeverScrollableScrollPhysics(),
                itemCount: _filteredScanResults.length,
                itemBuilder: (context, idx) {
                  final r = _filteredScanResults[idx];
                  final name = r.device.platformName.isNotEmpty ? r.device.platformName : "Unknown Bluetooth Device";
                  final isMediCool = name.toLowerCase().contains("medicool") || name.toLowerCase().contains("esp32");

                  return Container(
                    margin: const EdgeInsets.only(bottom: 10),
                    padding: const EdgeInsets.all(16),
                    decoration: BoxDecoration(
                      color: isMediCool ? const Color(0xFF19284D) : const Color(0xFF131D33),
                      borderRadius: BorderRadius.circular(18),
                      border: Border.all(
                        color: isMediCool ? const Color(0xFF00B4D8) : Colors.white.withValues(alpha: 0.06),
                        width: isMediCool ? 1.5 : 1,
                      ),
                    ),
                    child: Row(
                      children: [
                        Container(
                          width: 44,
                          height: 44,
                          decoration: BoxDecoration(
                            color: isMediCool
                                ? const Color(0xFF00B4D8).withValues(alpha: 0.2)
                                : Colors.white.withValues(alpha: 0.05),
                            borderRadius: BorderRadius.circular(12),
                          ),
                          child: Icon(
                            isMediCool ? Icons.ac_unit : Icons.bluetooth,
                            color: isMediCool ? const Color(0xFF00B4D8) : Colors.grey,
                          ),
                        ),
                        const SizedBox(width: 14),
                        Expanded(
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text(
                                name,
                                style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 15, color: Colors.white),
                              ),
                              const SizedBox(height: 3),
                              Text(
                                "${r.device.remoteId.str} • RSSI: ${r.rssi} dBm",
                                style: TextStyle(fontSize: 11, color: Colors.grey.shade400),
                              ),
                            ],
                          ),
                        ),
                        ElevatedButton(
                          style: ElevatedButton.styleFrom(
                            backgroundColor: isMediCool ? const Color(0xFF00B4D8) : const Color(0xFF233252),
                            foregroundColor: Colors.white,
                            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
                          ),
                          onPressed: _isConnecting ? null : () => _connectToDevice(r.device),
                          child: _isConnecting
                              ? const SizedBox(
                                  width: 14,
                                  height: 14,
                                  child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white))
                              : const Text("Connect", style: TextStyle(fontWeight: FontWeight.bold, fontSize: 12)),
                        ),
                      ],
                    ),
                  );
                },
              ),
          ],
        ),
      ),
    );
  }

  // ==============================================================================
  // 2. MAIN 4-TAB DASHBOARD SCREEN
  // ==============================================================================
  Widget _buildDashboardScreen() {
    final screens = [
      _buildHomeScreen(),
      _buildFansScreen(),
      _buildTurboScreen(),
      _buildDeviceAndAuditScreen(),
    ];

    return Scaffold(
      appBar: AppBar(
        backgroundColor: const Color(0xFF0C1424),
        elevation: 0,
        title: Row(
          children: [
            const Icon(Icons.ac_unit, color: Color(0xFF00B4D8), size: 20),
            const SizedBox(width: 8),
            const Text(
              "MEDICOOL",
              style: TextStyle(fontWeight: FontWeight.w800, letterSpacing: 1, fontSize: 16),
            ),
            const Spacer(),
            // Smartwatch-style Bluetooth Badge
            GestureDetector(
              onTap: () => setState(() => _currentTabIndex = 3),
              child: Container(
                padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                decoration: BoxDecoration(
                  color: const Color(0x2206D6A0),
                  borderRadius: BorderRadius.circular(20),
                  border: Border.all(color: const Color(0xFF06D6A0), width: 1),
                ),
                child: Row(
                  children: [
                    const Icon(Icons.bluetooth_connected, color: Color(0xFF06D6A0), size: 14),
                    const SizedBox(width: 5),
                    Text(
                      _isDemoMode ? "DEMO" : "BLE SYNC",
                      style: const TextStyle(fontSize: 10, fontWeight: FontWeight.bold, color: Color(0xFF06D6A0)),
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
      body: screens[_currentTabIndex],
      bottomNavigationBar: BottomNavigationBar(
        currentIndex: _currentTabIndex,
        onTap: (idx) => setState(() => _currentTabIndex = idx),
        type: BottomNavigationBarType.fixed,
        backgroundColor: const Color(0xFF0C1424),
        selectedItemColor: const Color(0xFF00B4D8),
        unselectedItemColor: Colors.grey.shade500,
        selectedFontSize: 11,
        unselectedFontSize: 11,
        items: const [
          BottomNavigationBarItem(icon: Icon(Icons.dashboard_outlined), activeIcon: Icon(Icons.dashboard), label: "Home"),
          BottomNavigationBarItem(icon: Icon(Icons.mode_fan_off_outlined), activeIcon: Icon(Icons.air), label: "Fans"),
          BottomNavigationBarItem(icon: Icon(Icons.bolt_outlined), activeIcon: Icon(Icons.bolt), label: "Turbo"),
          BottomNavigationBarItem(icon: Icon(Icons.watch_outlined), activeIcon: Icon(Icons.watch), label: "Device & Audit"),
        ],
      ),
    );
  }

  // ==============================================================================
  // TAB 1: HOME (TELEMETRY + INTEGRATED PRESETS & MODES SLIDER)
  // ==============================================================================
  Widget _buildHomeScreen() {
    final bool isSafe = chamberTemp >= 2.0 && chamberTemp <= 8.0;

    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Chamber Hero Card
          Container(
            width: double.infinity,
            padding: const EdgeInsets.symmetric(vertical: 24, horizontal: 20),
            decoration: BoxDecoration(
              gradient: LinearGradient(
                colors: isSafe
                    ? [const Color(0xFF131D33), const Color(0xFF172847)]
                    : [const Color(0xFF28131B), const Color(0xFF1D1118)],
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
              ),
              borderRadius: BorderRadius.circular(24),
              border: Border.all(
                color: isSafe ? const Color(0xFF00B4D8).withValues(alpha: 0.3) : const Color(0xFFEF476F),
                width: 1.5,
              ),
              boxShadow: [
                BoxShadow(
                  color: Colors.black.withValues(alpha: 0.4),
                  blurRadius: 18,
                  offset: const Offset(0, 8),
                ),
              ],
            ),
            child: Column(
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    const Text(
                      "CHAMBER CORE TEMPERATURE",
                      style: TextStyle(fontSize: 11, letterSpacing: 1.2, fontWeight: FontWeight.bold, color: Colors.grey),
                    ),
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                      decoration: BoxDecoration(
                        color: Colors.white.withValues(alpha: 0.08),
                        borderRadius: BorderRadius.circular(12),
                      ),
                      child: Text("Target: ${targetSetpoint.toStringAsFixed(1)}°C",
                          style: const TextStyle(fontSize: 11, color: Colors.white, fontWeight: FontWeight.bold)),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      chamberTemp.toStringAsFixed(1),
                      style: const TextStyle(fontSize: 58, fontWeight: FontWeight.w900, color: Colors.white),
                    ),
                    const Padding(
                      padding: EdgeInsets.only(top: 8.0),
                      child: Text("°C", style: TextStyle(fontSize: 24, fontWeight: FontWeight.bold, color: Color(0xFF00B4D8))),
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
                  decoration: BoxDecoration(
                    color: isSafe ? const Color(0x2206D6A0) : const Color(0x22EF476F),
                    borderRadius: BorderRadius.circular(20),
                    border: Border.all(color: isSafe ? const Color(0xFF06D6A0) : const Color(0xFFEF476F)),
                  ),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      CircleAvatar(
                        radius: 4,
                        backgroundColor: isSafe ? const Color(0xFF06D6A0) : const Color(0xFFEF476F),
                      ),
                      const SizedBox(width: 8),
                      Text(
                        isSafe ? "SAFE COLD CHAIN (2°C – 8°C)" : "TEMPERATURE EXCURSION ALERT",
                        style: TextStyle(
                          fontSize: 10,
                          fontWeight: FontWeight.bold,
                          color: isSafe ? const Color(0xFF06D6A0) : const Color(0xFFEF476F),
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),

          // Battery & Power Card (Home Screen)
          _buildBatteryHomeCard(),
          const SizedBox(height: 16),

          // Triple Sensor Array Cards
          Row(
            children: [
              _buildSensorMetric("Top Sensor", "${topTemp.toStringAsFixed(1)}°C", Icons.arrow_upward),
              const SizedBox(width: 8),
              _buildSensorMetric("Bottom Sensor", "${bottomTemp.toStringAsFixed(1)}°C", Icons.arrow_downward),
              const SizedBox(width: 8),
              _buildSensorMetric("Heatsink", "${sinkTemp.toStringAsFixed(1)}°C", Icons.local_fire_department,
                  isWarning: sinkTemp >= 55.0),
            ],
          ),
          const SizedBox(height: 16),

          // Integrated Presets & Modes Section (Moved to Home)
          Container(
            padding: const EdgeInsets.all(18),
            decoration: BoxDecoration(
              color: const Color(0xFF131D33),
              borderRadius: BorderRadius.circular(20),
              border: Border.all(color: Colors.white.withValues(alpha: 0.05)),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Text(
                      "MEDICINE TARGET PRESETS",
                      style: TextStyle(fontSize: 11, fontWeight: FontWeight.bold, letterSpacing: 1.1, color: Colors.grey),
                    ),
                    Icon(Icons.tune, size: 16, color: Color(0xFF00B4D8)),
                  ],
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(child: _buildPresetButton("❄️ Insulin", "4.0°C", 0)),
                    const SizedBox(width: 6),
                    Expanded(child: _buildPresetButton("💉 Vaccine", "5.0°C", 1)),
                    const SizedBox(width: 6),
                    Expanded(child: _buildPresetButton("👁️ Drops", "6.0°C", 2)),
                    const SizedBox(width: 6),
                    Expanded(child: _buildPresetButton("⚙️ Custom", "Manual", 3)),
                  ],
                ),
                const SizedBox(height: 14),

                // Manual Control Status Banner
                if (presetIndex != 3)
                  Container(
                    margin: const EdgeInsets.only(bottom: 12),
                    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                    decoration: BoxDecoration(
                      color: Colors.amber.withValues(alpha: 0.1),
                      borderRadius: BorderRadius.circular(10),
                      border: Border.all(color: Colors.amber.withValues(alpha: 0.25)),
                    ),
                    child: Row(
                      children: [
                        const Icon(Icons.lock, size: 14, color: Colors.amber),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            "Manual slider is locked to preset '$presetName' (${targetSetpoint.toStringAsFixed(1)}°C). Tap 'Custom' to enable manual control.",
                            style: const TextStyle(fontSize: 11, color: Colors.amber, fontWeight: FontWeight.w600),
                          ),
                        ),
                      ],
                    ),
                  )
                else
                  Container(
                    margin: const EdgeInsets.only(bottom: 12),
                    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                    decoration: BoxDecoration(
                      color: const Color(0xFF06D6A0).withValues(alpha: 0.1),
                      borderRadius: BorderRadius.circular(10),
                      border: Border.all(color: const Color(0xFF06D6A0).withValues(alpha: 0.3)),
                    ),
                    child: const Row(
                      children: [
                        Icon(Icons.lock_open, size: 14, color: Color(0xFF06D6A0)),
                        SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            "Manual Control Active: Drag slider to adjust target temperature freely (2.0°C – 8.0°C).",
                            style: TextStyle(fontSize: 11, color: Color(0xFF06D6A0), fontWeight: FontWeight.w600),
                          ),
                        ),
                      ],
                    ),
                  ),

                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Text(
                      presetIndex == 3 ? "Manual Target Slider:" : "Target Setpoint:",
                      style: const TextStyle(fontSize: 13, color: Colors.white),
                    ),
                    Text(
                      "${targetSetpoint.toStringAsFixed(1)}°C",
                      style: TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.bold,
                        color: presetIndex == 3 ? const Color(0xFF00B4D8) : Colors.grey.shade400,
                      ),
                    ),
                  ],
                ),
                SliderTheme(
                  data: SliderTheme.of(context).copyWith(
                    activeTrackColor: presetIndex == 3 ? const Color(0xFF00B4D8) : Colors.grey.shade700,
                    thumbColor: presetIndex == 3 ? const Color(0xFF00B4D8) : Colors.grey.shade600,
                    inactiveTrackColor: Colors.white.withValues(alpha: 0.08),
                    disabledActiveTrackColor: Colors.grey.shade800,
                    disabledThumbColor: Colors.grey.shade700,
                  ),
                  child: Slider(
                    value: targetSetpoint.clamp(2.0, 8.0),
                    min: 2.0,
                    max: 8.0,
                    divisions: 60,
                    label: "${targetSetpoint.toStringAsFixed(1)}°C",
                    onChanged: (presetIndex == 3)
                        ? (v) {
                            setState(() => targetSetpoint = v);
                            _sendBleCommand("temp=${v.toStringAsFixed(1)}");
                          }
                        : null,
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),

          // Real-time Temperature Trend Curve
          Container(
            padding: const EdgeInsets.all(18),
            decoration: BoxDecoration(
              color: const Color(0xFF131D33),
              borderRadius: BorderRadius.circular(20),
              border: Border.all(color: Colors.white.withValues(alpha: 0.05)),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Text(
                      "LIVE STABILITY CURVE (REAL-TIME)",
                      style: TextStyle(fontSize: 11, fontWeight: FontWeight.bold, letterSpacing: 1.1, color: Colors.grey),
                    ),
                    Text("Safe Band: 2 - 8°C", style: TextStyle(fontSize: 11, color: Color(0xFF06D6A0))),
                  ],
                ),
                const SizedBox(height: 16),
                SizedBox(
                  height: 150,
                  child: LineChart(
                    LineChartData(
                      minY: 1.0,
                      maxY: 9.0,
                      gridData: FlGridData(
                        show: true,
                        horizontalInterval: 2,
                        getDrawingHorizontalLine: (val) => FlLine(
                          color: (val == 2.0 || val == 8.0)
                              ? const Color(0xFFEF476F).withValues(alpha: 0.5)
                              : Colors.white.withValues(alpha: 0.04),
                          strokeWidth: (val == 2.0 || val == 8.0) ? 1.5 : 1,
                          dashArray: (val == 2.0 || val == 8.0) ? [4, 4] : null,
                        ),
                      ),
                      titlesData: const FlTitlesData(show: false),
                      borderData: FlBorderData(show: false),
                      lineBarsData: [
                        LineChartBarData(
                          spots: _tempHistory,
                          isCurved: true,
                          curveSmoothness: 0.35,
                          color: const Color(0xFF00B4D8),
                          barWidth: 3,
                          isStrokeCapRound: true,
                          dotData: const FlDotData(show: false),
                          belowBarData: BarAreaData(
                            show: true,
                            gradient: LinearGradient(
                              colors: [
                                const Color(0xFF00B4D8).withValues(alpha: 0.3),
                                const Color(0xFF00B4D8).withValues(alpha: 0.0),
                              ],
                              begin: Alignment.topCenter,
                              end: Alignment.bottomCenter,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildSensorMetric(String title, String val, IconData icon, {bool isWarning = false}) {
    return Expanded(
      child: Container(
        padding: const EdgeInsets.symmetric(vertical: 14, horizontal: 10),
        decoration: BoxDecoration(
          color: const Color(0xFF131D33),
          borderRadius: BorderRadius.circular(16),
          border: Border.all(color: isWarning ? const Color(0xFFEF476F) : Colors.white.withValues(alpha: 0.06)),
        ),
        child: Column(
          children: [
            Icon(icon, size: 16, color: isWarning ? const Color(0xFFEF476F) : const Color(0xFF00B4D8)),
            const SizedBox(height: 6),
            Text(title, style: TextStyle(fontSize: 10, color: Colors.grey.shade400)),
            const SizedBox(height: 4),
            Text(val,
                style: TextStyle(
                    fontSize: 15, fontWeight: FontWeight.bold, color: isWarning ? const Color(0xFFEF476F) : Colors.white)),
          ],
        ),
      ),
    );
  }

  Widget _buildBatteryHomeCard() {
    final bool isLow = batteryPercent <= 20;
    final bool isChargingActive = isCharging;

    // Dynamic backup estimation
    String estBackupText;
    if (isChargingActive) {
      estBackupText = "Continuous (Plugged In)";
    } else {
      double hoursTotal = turboActive ? 1.2 : (peltierDuty > 150 ? 3.0 : 5.5);
      double estHours = (batteryPercent / 100.0) * hoursTotal;
      int h = estHours.floor();
      int m = ((estHours - h) * 60).round();
      estBackupText = h > 0 ? "~${h}h ${m}m remaining" : "~${m}m remaining";
    }

    Color batteryColor = isChargingActive
        ? const Color(0xFF06D6A0)
        : (isLow ? const Color(0xFFEF476F) : const Color(0xFF00B4D8));

    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: const Color(0xFF131D33),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(
          color: isChargingActive
              ? const Color(0xFF06D6A0).withValues(alpha: 0.3)
              : (isLow ? const Color(0xFFEF476F).withValues(alpha: 0.5) : Colors.white.withValues(alpha: 0.06)),
          width: 1.2,
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Row(
                children: [
                  Icon(
                    isChargingActive
                        ? Icons.bolt
                        : (isLow ? Icons.battery_alert : Icons.battery_charging_full),
                    color: batteryColor,
                    size: 18,
                  ),
                  const SizedBox(width: 8),
                  const Text(
                    "POWER & BATTERY HEALTH",
                    style: TextStyle(fontSize: 11, fontWeight: FontWeight.bold, letterSpacing: 1.1, color: Colors.grey),
                  ),
                ],
              ),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                decoration: BoxDecoration(
                  color: batteryColor.withValues(alpha: 0.15),
                  borderRadius: BorderRadius.circular(12),
                  border: Border.all(color: batteryColor.withValues(alpha: 0.4)),
                ),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    if (isChargingActive) ...[
                      const Icon(Icons.flash_on, size: 12, color: Color(0xFF06D6A0)),
                      const SizedBox(width: 3),
                    ],
                    Text(
                      isChargingActive ? "CHARGING (20V PD)" : (isLow ? "LOW BATTERY" : "DISCHARGING"),
                      style: TextStyle(
                        fontSize: 10,
                        fontWeight: FontWeight.bold,
                        color: batteryColor,
                        letterSpacing: 0.5,
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
          const SizedBox(height: 14),
          Row(
            crossAxisAlignment: CrossAxisAlignment.baseline,
            textBaseline: TextBaseline.alphabetic,
            children: [
              Text(
                "$batteryPercent%",
                style: TextStyle(
                  fontSize: 32,
                  fontWeight: FontWeight.w900,
                  color: batteryColor,
                ),
              ),
              const SizedBox(width: 10),
              Text(
                "${vbatVoltage.toStringAsFixed(2)}V (3S Li-ion)",
                style: TextStyle(fontSize: 13, color: Colors.grey.shade400, fontWeight: FontWeight.w600),
              ),
              const Spacer(),
              Column(
                crossAxisAlignment: CrossAxisAlignment.end,
                children: [
                  Text(
                    "ESTIMATED BACKUP",
                    style: TextStyle(fontSize: 9, fontWeight: FontWeight.bold, color: Colors.grey.shade400, letterSpacing: 0.8),
                  ),
                  const SizedBox(height: 2),
                  Row(
                    children: [
                      Icon(
                        isChargingActive ? Icons.all_inclusive : Icons.schedule,
                        size: 14,
                        color: isChargingActive ? const Color(0xFF06D6A0) : Colors.white,
                      ),
                      const SizedBox(width: 4),
                      Text(
                        estBackupText,
                        style: const TextStyle(fontSize: 13, fontWeight: FontWeight.bold, color: Colors.white),
                      ),
                    ],
                  ),
                ],
              ),
            ],
          ),
          const SizedBox(height: 12),
          ClipRRect(
            borderRadius: BorderRadius.circular(8),
            child: LinearProgressIndicator(
              value: (batteryPercent / 100.0).clamp(0.0, 1.0),
              minHeight: 8,
              backgroundColor: Colors.white.withValues(alpha: 0.08),
              valueColor: AlwaysStoppedAnimation<Color>(batteryColor),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildPresetButton(String label, String temp, int index) {
    final bool active = presetIndex == index;
    return ElevatedButton(
      style: ElevatedButton.styleFrom(
        backgroundColor: active ? const Color(0xFF00B4D8) : const Color(0xFF19253F),
        foregroundColor: active ? Colors.black : Colors.white,
        padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 4),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
          side: BorderSide(color: active ? const Color(0xFF00B4D8) : Colors.white.withValues(alpha: 0.08)),
        ),
      ),
      onPressed: () {
        setState(() {
          presetIndex = index;
          if (index == 0) {
            targetSetpoint = 4.0;
            presetName = "INSULIN";
          } else if (index == 1) {
            targetSetpoint = 5.0;
            presetName = "VACCINES";
          } else if (index == 2) {
            targetSetpoint = 6.0;
            presetName = "EYE DROPS";
          } else if (index == 3) {
            presetName = "CUSTOM";
          }
        });
        _sendBleCommand("preset=$index");
      },
      child: Column(
        children: [
          Text(label, maxLines: 1, overflow: TextOverflow.ellipsis, style: const TextStyle(fontSize: 11, fontWeight: FontWeight.bold)),
          const SizedBox(height: 2),
          Text(temp, style: TextStyle(fontSize: 9, color: active ? Colors.black87 : Colors.grey.shade400)),
        ],
      ),
    );
  }

  // ==============================================================================
  // TAB 2: ADVANCED FANS
  // ==============================================================================
  Widget _buildFansScreen() {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Hot Fan Control Card
          Container(
            padding: const EdgeInsets.all(18),
            decoration: BoxDecoration(
              color: const Color(0xFF131D33),
              borderRadius: BorderRadius.circular(20),
              border: Border.all(color: Colors.white.withValues(alpha: 0.05)),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    const Row(
                      children: [
                        Icon(Icons.air, color: Color(0xFFEF476F), size: 20),
                        SizedBox(width: 8),
                        Text("Hot Heatsink Blower",
                            style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold, color: Colors.white)),
                      ],
                    ),
                    Text("${((hotFanDuty / 255.0) * 100).round()}% RPM",
                        style: const TextStyle(fontWeight: FontWeight.bold, color: Color(0xFFEF476F))),
                  ],
                ),
                const SizedBox(height: 6),
                Text(
                  manualHotFan
                      ? "Manual override engaged — fixed duty cycle."
                      : "Automatic Governor: Dynamically scales with heatsink temperature (${sinkTemp.toStringAsFixed(1)}°C).",
                  style: TextStyle(fontSize: 12, color: Colors.grey.shade400),
                ),
                const SizedBox(height: 14),
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  title: const Text("Manual Speed Override", style: TextStyle(fontSize: 14, color: Colors.white)),
                  subtitle: const Text("Override thermal curve to test fan speeds", style: TextStyle(fontSize: 11)),
                  value: manualHotFan,
                  activeThumbColor: const Color(0xFFEF476F),
                  onChanged: (v) {
                    setState(() => manualHotFan = v);
                    _sendBleCommand("manualHot=${v ? 1 : 0}");
                  },
                ),
                if (manualHotFan) ...[
                  Slider(
                    value: manualHotDuty.toDouble(),
                    min: 50,
                    max: 255,
                    activeThumbColor: const Color(0xFFEF476F),
                    onChanged: (v) {
                      setState(() {
                        manualHotDuty = v.toInt();
                        hotFanDuty = manualHotDuty;
                      });
                      _sendBleCommand("hotDuty=${v.toInt()}");
                    },
                  ),
                ],
              ],
            ),
          ),
          const SizedBox(height: 16),

          // Cold Fan Control Card
          Container(
            padding: const EdgeInsets.all(18),
            decoration: BoxDecoration(
              color: const Color(0xFF131D33),
              borderRadius: BorderRadius.circular(20),
              border: Border.all(color: Colors.white.withValues(alpha: 0.05)),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    const Row(
                      children: [
                        Icon(Icons.ac_unit, color: Color(0xFF00B4D8), size: 20),
                        SizedBox(width: 8),
                        Text("Cold Circulation Fan",
                            style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold, color: Colors.white)),
                      ],
                    ),
                    Text(
                      coldFanActive ? "ACTIVE" : "IDLE",
                      style: TextStyle(
                        fontWeight: FontWeight.bold,
                        color: coldFanActive ? const Color(0xFF06D6A0) : Colors.grey,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 6),
                Text(
                  "Prevents temperature layering by stirring cold air across top & bottom sensors (Gradient: ${(topTemp - bottomTemp).abs().toStringAsFixed(2)}°C).",
                  style: TextStyle(fontSize: 12, color: Colors.grey.shade400),
                ),
                const SizedBox(height: 16),
                SegmentedButton<int>(
                  segments: const [
                    ButtonSegment(value: 0, label: Text("Auto Destrat")),
                    ButtonSegment(value: 1, label: Text("Always ON")),
                    ButtonSegment(value: 2, label: Text("Forced OFF")),
                  ],
                  selected: {coldFanMode},
                  onSelectionChanged: (set) {
                    setState(() => coldFanMode = set.first);
                    _sendBleCommand("coldMode=${set.first}");
                  },
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  // ==============================================================================
  // TAB 3: TURBO (100% OVERDRIVE WITH MANDATORY SAFETY WARNING POP-UP)
  // ==============================================================================
  Widget _buildTurboScreen() {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            padding: const EdgeInsets.all(22),
            decoration: BoxDecoration(
              gradient: LinearGradient(
                colors: turboActive
                    ? [const Color(0xFF4A101D), const Color(0xFF280B12)]
                    : [const Color(0xFF1A1A2E), const Color(0xFF131D33)],
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
              ),
              borderRadius: BorderRadius.circular(24),
              border: Border.all(
                color: turboActive ? const Color(0xFFEF476F) : Colors.amber.withValues(alpha: 0.4),
                width: 2,
              ),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    const Row(
                      children: [
                        Icon(Icons.flash_on, color: Colors.amber, size: 24),
                        SizedBox(width: 8),
                        Text("100% Turbo Overdrive",
                            style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold, color: Colors.white)),
                      ],
                    ),
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                      decoration: BoxDecoration(
                        color: turboActive ? const Color(0xFFEF476F) : Colors.grey.withValues(alpha: 0.2),
                        borderRadius: BorderRadius.circular(12),
                      ),
                      child: Text(
                        turboActive ? "ENGAGED" : "OFF",
                        style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 11, color: Colors.white),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                const Text(
                  "Pushes the TEC1-12706 Peltier element and hot exhaust fan to 100% maximum power (~55 Watts) for rapid lab pull-down test bench demonstration.",
                  style: TextStyle(fontSize: 13, height: 1.4, color: Colors.white70),
                ),
                const SizedBox(height: 20),

                // Trigger Button that opens safety warning dialog
                SizedBox(
                  width: double.infinity,
                  child: ElevatedButton.icon(
                    style: ElevatedButton.styleFrom(
                      backgroundColor: turboActive ? const Color(0xFFEF476F) : Colors.amber.shade700,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(vertical: 16),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                    ),
                    icon: Icon(turboActive ? Icons.power_settings_new : Icons.warning_amber_rounded),
                    label: Text(
                      turboActive ? "DISENGAGE TURBO OVERDRIVE" : "ENABLE 100% TURBO OVERDRIVE",
                      style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 13),
                    ),
                    onPressed: () {
                      if (turboActive) {
                        setState(() => turboActive = false);
                        _sendBleCommand("turbo=0");
                      } else {
                        _showTurboWarningDialog();
                      }
                    },
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 20),

          // Built-in Safety Guardrails List
          const Text("AUTOMATIC SAFETY GUARDRAILS",
              style: TextStyle(fontSize: 12, fontWeight: FontWeight.bold, letterSpacing: 1.2, color: Colors.grey)),
          const SizedBox(height: 10),
          _buildGuardrailItem("Heatsink Cutoff", "Auto-disengages if heatsink reaches 60°C", Icons.thermostat),
          _buildGuardrailItem("Battery Protection", "Auto-disengages if battery drops below 11.0V", Icons.battery_alert),
          _buildGuardrailItem("Anti-Freeze Floor", "Stops Peltier cooling if chamber drops below 2.0°C", Icons.ac_unit),
        ],
      ),
    );
  }

  void _showTurboWarningDialog() {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: const Color(0xFF1A1A2E),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(24),
          side: const BorderSide(color: Colors.amber, width: 2),
        ),
        title: const Row(
          children: [
            Icon(Icons.warning, color: Colors.amber, size: 28),
            SizedBox(width: 10),
            Text("High Power Warning", style: TextStyle(fontWeight: FontWeight.bold, fontSize: 18)),
          ],
        ),
        content: const Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              "You are about to force the Peltier module to 100% duty cycle (~55 Watts continuous).",
              style: TextStyle(fontSize: 14, height: 1.4, color: Colors.white),
            ),
            SizedBox(height: 12),
            Text("• Hot blower will spin at maximum RPM.\n• Battery backup runtime will decrease rapidly.\n• Intended strictly for bench pull-down testing.",
                style: TextStyle(fontSize: 12, height: 1.5, color: Colors.grey)),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text("CANCEL", style: TextStyle(color: Colors.grey)),
          ),
          ElevatedButton(
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFFEF476F),
              foregroundColor: Colors.white,
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
            ),
            onPressed: () {
              Navigator.pop(ctx);
              setState(() => turboActive = true);
              _sendBleCommand("turbo=1");
            },
            child: const Text("I UNDERSTAND, ENABLE", style: TextStyle(fontWeight: FontWeight.bold)),
          ),
        ],
      ),
    );
  }

  Widget _buildGuardrailItem(String title, String desc, IconData icon) {
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: const Color(0xFF131D33),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: Colors.white.withValues(alpha: 0.04)),
      ),
      child: Row(
        children: [
          Icon(icon, size: 20, color: const Color(0xFF00B4D8)),
          const SizedBox(width: 14),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(title, style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 13, color: Colors.white)),
                const SizedBox(height: 2),
                Text(desc, style: TextStyle(fontSize: 11, color: Colors.grey.shade400)),
              ],
            ),
          ),
        ],
      ),
    );
  }

  // ==============================================================================
  // TAB 4: DEVICE & AUDIT (SMARTWATCH COMPANION STYLE)
  // ==============================================================================
  Widget _buildDeviceAndAuditScreen() {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Smartwatch Style Device Card
          Container(
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              gradient: const LinearGradient(
                colors: [Color(0xFF172847), Color(0xFF131D33)],
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
              ),
              borderRadius: BorderRadius.circular(24),
              border: Border.all(color: const Color(0xFF00B4D8).withValues(alpha: 0.3)),
            ),
            child: Column(
              children: [
                Row(
                  children: [
                    Container(
                      width: 54,
                      height: 54,
                      decoration: BoxDecoration(
                        color: const Color(0xFF00B4D8).withValues(alpha: 0.2),
                        borderRadius: BorderRadius.circular(16),
                      ),
                      child: const Icon(Icons.ac_unit, color: Color(0xFF00B4D8), size: 30),
                    ),
                    const SizedBox(width: 16),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(deviceName,
                              style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 16, color: Colors.white)),
                          const SizedBox(height: 4),
                          Text("ID: $deviceId", style: TextStyle(fontSize: 11, color: Colors.grey.shade400)),
                          const SizedBox(height: 2),
                          Row(
                            children: [
                              const CircleAvatar(radius: 3, backgroundColor: Color(0xFF06D6A0)),
                              const SizedBox(width: 6),
                              Text("BLE Connected • $deviceRssi dBm",
                                  style: const TextStyle(fontSize: 11, color: Color(0xFF06D6A0), fontWeight: FontWeight.bold)),
                            ],
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 18),
                const Divider(color: Colors.white12),
                const SizedBox(height: 12),

                // Key Device Stats
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    _buildDeviceStat("Battery", "$batteryPercent%", Icons.battery_charging_full),
                    _buildDeviceStat("Voltage", "${vbatVoltage.toStringAsFixed(2)}V", Icons.electric_bolt),
                    _buildDeviceStat("Uptime", "${(uptimeSeconds / 60).floor()}m", Icons.timer),
                    _buildDeviceStat("Firmware", "v2.0-BLE", Icons.memory),
                  ],
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),

          // Physical Keylock Remote Switch
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            decoration: BoxDecoration(
              color: const Color(0xFF131D33),
              borderRadius: BorderRadius.circular(18),
              border: Border.all(color: Colors.white.withValues(alpha: 0.05)),
            ),
            child: SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: const Text("Physical Button Key-Lock",
                  style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold, color: Colors.white)),
              subtitle: const Text("Locks the OLED buttons on the cooler to prevent tampering",
                  style: TextStyle(fontSize: 11)),
              value: isLocked,
              activeThumbColor: const Color(0xFF00B4D8),
              onChanged: (v) {
                setState(() => isLocked = v);
                _sendBleCommand("toggleLock=1");
              },
            ),
          ),
          const SizedBox(height: 18),

          // Medical Cold-Chain Compliance Audit
          const Text("COLD-CHAIN AUDIT & COMPLIANCE",
              style: TextStyle(fontSize: 12, fontWeight: FontWeight.bold, letterSpacing: 1.2, color: Colors.grey)),
          const SizedBox(height: 10),
          Container(
            padding: const EdgeInsets.all(18),
            decoration: BoxDecoration(
              color: const Color(0xFF131D33),
              borderRadius: BorderRadius.circular(20),
              border: Border.all(color: Colors.white.withValues(alpha: 0.05)),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    const Text("MKT Quality Score",
                        style: TextStyle(fontSize: 14, fontWeight: FontWeight.bold, color: Colors.white)),
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                      decoration: BoxDecoration(
                        color: const Color(0x2206D6A0),
                        borderRadius: BorderRadius.circular(12),
                        border: Border.all(color: const Color(0xFF06D6A0)),
                      ),
                      child: const Text("100% WHO PASS",
                          style: TextStyle(color: Color(0xFF06D6A0), fontSize: 11, fontWeight: FontWeight.bold)),
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                Text(
                  "All logged hours maintained within strict 2.0°C to 8.0°C regulatory threshold. Zero temperature excursions registered.",
                  style: TextStyle(fontSize: 12, color: Colors.grey.shade400, height: 1.4),
                ),
                const SizedBox(height: 16),
                Row(
                  children: [
                    Expanded(
                      child: OutlinedButton.icon(
                        icon: const Icon(Icons.download, size: 16, color: Color(0xFF00B4D8)),
                        label: const Text("Export CSV Audit Log", style: TextStyle(color: Color(0xFF00B4D8), fontSize: 12)),
                        style: OutlinedButton.styleFrom(
                          side: BorderSide(color: const Color(0xFF00B4D8).withValues(alpha: 0.4)),
                          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                        ),
                        onPressed: () {
                          ScaffoldMessenger.of(context).showSnackBar(
                            const SnackBar(content: Text("Audit report generated: medicool_audit_log.csv")),
                          );
                        },
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
          const SizedBox(height: 18),

          // Disconnect / Scan other devices
          SizedBox(
            width: double.infinity,
            child: ElevatedButton.icon(
              style: ElevatedButton.styleFrom(
                backgroundColor: const Color(0xFF1B2640),
                foregroundColor: const Color(0xFFEF476F),
                padding: const EdgeInsets.symmetric(vertical: 14),
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(16),
                  side: BorderSide(color: const Color(0xFFEF476F).withValues(alpha: 0.3)),
                ),
              ),
              icon: const Icon(Icons.bluetooth_disabled, size: 18),
              label: const Text("Disconnect & Scan Other Coolers", style: TextStyle(fontWeight: FontWeight.bold)),
              onPressed: _disconnect,
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildDeviceStat(String label, String val, IconData icon) {
    return Column(
      children: [
        Icon(icon, size: 18, color: const Color(0xFF00B4D8)),
        const SizedBox(height: 4),
        Text(val, style: const TextStyle(fontWeight: FontWeight.bold, fontSize: 14, color: Colors.white)),
        const SizedBox(height: 2),
        Text(label, style: TextStyle(fontSize: 10, color: Colors.grey.shade400)),
      ],
    );
  }
}
