import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:geolocator/geolocator.dart';
import 'package:permission_handler/permission_handler.dart';
import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'dart:typed_data';
import 'package:http/http.dart' as http;
import 'dart:io'; 

// --- Modelo de Datos ---
class AstroObject {
  final String name;
  final String constellation;
  final String ra;
  final String dec;
  final String info;
  final String imageUrl;

  AstroObject({
    required this.name,
    required this.constellation,
    required this.ra,
    required this.dec,
    required this.info,
    required this.imageUrl,
  });

  factory AstroObject.fromJson(Map<String, dynamic> json) {
    return AstroObject(
      name: json['nombre'] ?? 'Sin Nombre',
      constellation: json['constelacion'] ?? 'Sin Constelación',
      info: json['info'] ?? 'Sin Informacion',
      imageUrl: json['imageUrl'] ?? 'Sin Imagen',
      ra: json['ra'] ?? 'N/A',
      dec: json['dec'] ?? 'N/A',
    );
  }
}

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const Horizonte());
}

class Horizonte extends StatelessWidget {
  const Horizonte({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'Horizonte',
      theme: ThemeData.dark().copyWith(
        primaryColor: const Color(0xFF1A237E),
        scaffoldBackgroundColor: const Color(0xFF0D1117),
        cardTheme: const CardThemeData(
          color: Color(0xFF161B22),
          elevation: 4,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.all(Radius.circular(16)),
          ),
        ),
        appBarTheme: const AppBarTheme(
          backgroundColor: Color(0xFF161B22),
          elevation: 0,
        ),
      ),
      home: const HomeScreen(),
    );
  }
}

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});
  @override
  _HomeScreenState createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  // --- Variables de Estado ---
  BluetoothAdapterState _adapterState = BluetoothAdapterState.unknown;
  StreamSubscription<BluetoothAdapterState>? _adapterStateSubscription;

  BluetoothDevice? _targetDevice;
  BluetoothCharacteristic? _targetCharacteristic;
  StreamSubscription<BluetoothConnectionState>? _deviceStateSubscription;
  StreamSubscription<Position>? _positionStreamSubscription;

  bool _isConnecting = false;
  bool _isDisconnecting = false; 
  bool get isConnected => _targetCharacteristic != null;

  Timer? _guidanceTimer;
  Timer? _timer;
  double? _currentTelescopeAzimut;
  double? _currentTelescopeAltitud;

  Map<String, double>? _targetAltAz;

  double _deltaAz = 0.0;
  double _deltaAlt = 0.0;
  bool _isInRange = false;
  bool _isTracking = false;

  static const double _errorMargin = 15.0;

  final Guid SERVICE_UUID = Guid("0000FFE0-0000-1000-8000-00805F9B34FB");
  final Guid CHARACTERISTIC_UUID = Guid("0000FFE2-0000-1000-8000-00805F9B34FB");

  Position? posicion;
  List<AstroObject> astroObjects = [];
  AstroObject? selectedObject;
  bool isLoading = true;
  bool _isInitialized = false; 
  bool isLoadingData = false;
  bool _isstall = false;

  @override
  void initState() {
    super.initState();

    WidgetsBinding.instance.addPostFrameCallback((_) {
      _initialize();
    });

    _adapterStateSubscription = FlutterBluePlus.adapterState.listen((state) {
      if (mounted) {
        setState(() {
          _adapterState = state;
        });
      }
    });

    _iniciarLocationStream();



    _guidanceTimer = Timer.periodic(const Duration(seconds: 1), (timer) {
      if (mounted) {
        _updateGuidance(); 
        
        if (_isTracking && isConnected && _targetAltAz != null) {
          _enviarCoordenadas(_targetAltAz!);
        }
      }
    });
  }

  Future<void> _iniciarLocationStream() async {
    // Configuración para el streaming de ubicación
    const LocationSettings locationSettings = LocationSettings(
      accuracy: LocationAccuracy.high,
      distanceFilter: 100, // Notificar solo si se mueve 100 metros
    );

    // Cancelar cualquier stream anterior (por si acaso)
    await _positionStreamSubscription?.cancel();

    // Iniciar el nuevo stream
    _positionStreamSubscription = Geolocator.getPositionStream(
      locationSettings: locationSettings
    ).handleError((error) {
      // Manejar errores del stream (ej. el usuario desactiva el GPS)
      debugPrint("Error en el stream de ubicación: $error");
      if (mounted) {
        setState(() => posicion = null); // Poner el GPS en modo "buscando"
      }
    }).listen((Position pos) {
      // ¡UBICACIÓN RECIBIDA!
      debugPrint('Stream de ubicación: ${pos.latitude}, ${pos.longitude}');
      if (mounted) {
        setState(() => posicion = pos);
        _updateGuidance(); // Actualizar cálculos
      }
    });
  }

  @override
  void dispose() {
    _adapterStateSubscription?.cancel();
    _deviceStateSubscription?.cancel();
    _positionStreamSubscription?.cancel();
    _targetDevice?.disconnect();
    _guidanceTimer?.cancel();
    super.dispose();
  }

  Future<void> _initialize() async {
    await _checkPermissionsAndServices();
    _fetchAstroData();
    if (mounted) {
      setState(() {
        _isInitialized = true;
      });
    }
  }

  Future<void> _checkPermissionsAndServices() async {
    debugPrint("--- Empezando la inicialización ---");
    await [
      Permission.location,
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
    ].request();
    debugPrint("Paso 1/7: Permisos solicitados.");

    debugPrint("Paso 2/7: Listener de Bluetooth configurado.");
    
    debugPrint("Paso 3/7: Verificando si la ubicación está activada...");
    bool isLocationEnabled = await Geolocator.isLocationServiceEnabled();
    debugPrint("Paso 4/7: Verificación de ubicación completa. ¿Activada?: $isLocationEnabled");

    debugPrint("Paso 5/7: Verificando estado del Bluetooth...");
    _adapterState = await FlutterBluePlus.adapterState.first;
    debugPrint("Paso 6/7: Verificación de Bluetooth completa. Estado: $_adapterState");

    if (_adapterState != BluetoothAdapterState.on) {
      if (mounted) await _showServiceDialog(
        "Activar Bluetooth", "Para continuar, por favor activa el Bluetooth.",
        () async { if (Platform.isAndroid) await FlutterBluePlus.turnOn(); }
      );
    }

    if (!isLocationEnabled) {
      if (mounted) await _showServiceDialog(
        "Activar Ubicación", "Se requiere la ubicación para los cálculos.",
        Geolocator.openLocationSettings);
    }

    await _obtenerUbicacion();
    debugPrint("Paso 7/7: Ubicación obtenida. --- INICIALIZACIÓN COMPLETA ---");
  }

  Future<void> _showServiceDialog(String title, String content,
      Future<void> Function() onConfirm) async {
    return showDialog<void>(
      context: context,
      barrierDismissible: false, 
      builder: (BuildContext context) {
        return AlertDialog(
          title: Text(title),
          content: SingleChildScrollView(
            child: ListBody(
              children: <Widget>[
                Text(content),
              ],
            ),
          ),
          actions: <Widget>[
            TextButton(
              child: const Text('Cancelar'),
              onPressed: () {
                Navigator.of(context).pop();
              },
            ),
            TextButton(
              child: const Text('Ir a Ajustes'),
              onPressed: () async {
                await onConfirm();
                Navigator.of(context).pop();
              },
            ),
          ],
        );
      },
    );
  }

  Future<void> _fetchAstroData() async {
    if (mounted) setState(() => isLoadingData = true);

    final url = Uri.parse('http://192.168.1.115:8000/api/estrellas/');
    selectedObject = null;
    try {
      final response = await http.get(url).timeout(const Duration(seconds: 10));

      if (response.statusCode == 200) {
        final List<dynamic> data = jsonDecode(response.body);
        final List<AstroObject> fetchedData =
        data.map((jsonItem) => AstroObject.fromJson(jsonItem)).toList();
        if (mounted) setState(() => astroObjects = fetchedData);
      } else {
        throw Exception('Fallo al cargar datos: ${response.statusCode}');
      }
    } on TimeoutException catch (_) {
      debugPrint("Timeout: no se pudo conectar al servidor, usando datos locales.");
      _usarDatosLocales();
    } on SocketException catch (_) {
      debugPrint("Sin conexión: Wi-Fi apagado o servidor apagado, usando datos locales.");
      _usarDatosLocales();
    } catch (e) {
      debugPrint("Error inesperado: $e");
      _usarDatosLocales();
    } finally {
      if (mounted) setState(() => isLoadingData = false);
    }
  }

  void _usarDatosLocales() {
    if (mounted) setState(() {
      selectedObject = null;
      astroObjects = [
        AstroObject(
          name: 'Arcturus',
          constellation: 'Boötes',
          ra: '14h 15m 39.7s',
          dec: '+19° 10′ 56″',
          info: 'Arcturus es una gigante naranja, la estrella más brillante del hemisferio norte.',
          imageUrl: 'assets/images/Arcturus.jpg',
        ),
        AstroObject(
          name: 'Capella',
          constellation: 'Auriga',
          ra: '05h 16m 41.3s',
          dec: '+45° 59′ 53″',
          info: 'Capella es un sistema de cuatro estrellas, visible como una sola desde la Tierra.',
          imageUrl: 'assets/images/Capella.jpg',
        ),
        AstroObject(
          name: 'Aldebaran',
          constellation: 'Taurus',
          ra: '04h 35m 55.2s',
          dec: '+16° 30′ 33″',
          info: 'Aldebaran, el ojo del toro, es una gigante roja cercana y brillante.',
          imageUrl: 'assets/images/Alpha.webp',
        ),
      ];
    });
  }

  Future<void> _reintentarConexion() async{
    await Future.delayed(Duration(seconds: 2));
    _fetchAstroData();
    return;
  }

  Future<void> _conectarODesconectar() async {
    if (isConnected) {
      if (_isDisconnecting) return; 

      setState(() {
        _isDisconnecting = true;
        _isTracking = false; // FIX: Detener tracking al desconectar
        _currentTelescopeAzimut = null; // FIX: Resetear posición
        _currentTelescopeAltitud = null;
        _isInRange = false;

      });

      try {
        await _targetDevice?.disconnect();
      } catch (e) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('Error al desconectar: $e'))
          );
        }
      } finally {
        if (mounted) {
          setState(() {
            _isDisconnecting = false;
          });
        }
      }
    } else {
      if (_isConnecting) return; 
      _scanAndConnect();
    }
  }

  void _scanAndConnect() async {
    if (mounted) setState(() => _isConnecting = true);

    BluetoothDevice? foundDevice; 
    StreamSubscription? scanSubscription;

    try {
      scanSubscription = FlutterBluePlus.scanResults.listen(
        (results) {
          if (foundDevice != null) return;
          for (ScanResult r in results) {
            if (r.device.platformName == 'BT04-A') {
              print('¡Dispositivo BT04-A encontrado durante el escaneo!');
              foundDevice = r.device;
              FlutterBluePlus.stopScan();
            }
          }
        },
      );

      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 10));
      scanSubscription.cancel();

      if (foundDevice == null) {
        throw "No se encontró el dispositivo BT04-A.";
      }

      print("Dispositivo encontrado. Iniciando secuencia de conexión...");

      _deviceStateSubscription = foundDevice!.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          if (mounted) {
            if (!_isDisconnecting) {
              ScaffoldMessenger.of(context).showSnackBar(
                const SnackBar(content: Text('Dispositivo se desconectó.'))
              );
            }
            setState(() {
              _targetDevice = null;
              _targetCharacteristic = null;
              _isTracking = false; // FIX: Resetear tracking
              _currentTelescopeAzimut = null; // FIX: Resetear posición
              _currentTelescopeAltitud = null;
              _isInRange = false;
            });
          }
        }
      });

      await foundDevice!.connect(autoConnect: false);
      print("Paso 1/2: Conexión física establecida.");

      List<BluetoothService> services = await foundDevice!.discoverServices();
      print("Paso 2/2: Servicios descubiertos con éxito.");

      for (var service in services) {
        if (service.uuid == SERVICE_UUID) {
          for (var characteristic in service.characteristics) {
            if (characteristic.uuid == CHARACTERISTIC_UUID) {
              print("¡ÉXITO TOTAL! Característica encontrada. La app está lista.");
              try {
                await characteristic.setNotifyValue(true);
                characteristic.onValueReceived.listen(_onBleDataReceived);
                print("Notificaciones habilitadas para ${characteristic.uuid}");
              } catch (e) {
                print("Error al habilitar notificaciones: $e");
                throw "Error al suscribirse a notificaciones.";
              }

              if (mounted) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('✓ Conectado exitosamente'))
                );
                setState(() {
                  _targetDevice = foundDevice; 
                  _targetCharacteristic = characteristic;
                  _isConnecting = false;
                });
              }
              return;
            }
          }
        }
      }

      throw "Característica FFE2 no encontrada en el servicio FFE0.";

    } catch (e) {
      print("ERROR en la secuencia de conexión: $e");
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Fallo en la conexión: $e'))
        );
        foundDevice?.disconnect();
        setState(() => _isConnecting = false);
      }
    }
  }

  void _onBleDataReceived(List<int> value) {
    try {
      String dataString = utf8.decode(value).trim();
      debugPrint("BLE Received: $dataString");
  
      var parts = dataString.split(',');
      if (parts.length == 2) {

        if (parts[0] == "STALL") {
        _isstall = true;
        // Detener el tracking en la app
        setState(() {
          _isTracking = false;
        });

        // Mostrar un diálogo de error al usuario
        if(_isstall){
          showDialog(
            context: context,
            builder: (context) => AlertDialog(
              title: Text("¡Límite Físico Alcanzado!"),
              content: Text(
                  "El motor de seguimiento ha llegado al tope de la perilla y se ha detenido por seguridad.\n\n"
                  "Para continuar, por favor:\n"
                  "1. Apunte manualmente el telescopio.\n"
                  "2. Gire la perilla de ajuste a su POSICIÓN CENTRAL."),
              actions: [
                TextButton(
                  child: Text("Entendido"),
                  onPressed: () => Navigator.of(context).pop(),
                ),
              ],
            ),
          );
        }
        return; // No intentes parsear "STALL" como un número
      }


        double az = double.parse(parts[0]);
        double alt = double.parse(parts[1]);
        
        if (mounted) {
          setState(() {
            _currentTelescopeAzimut = az;
            _currentTelescopeAltitud = alt;
          });
        }
      }
    } catch (e) {
      debugPrint("Error al parsear datos de BLE: $e");
    }
  }
  
  void _updateGuidance() {
    if (selectedObject == null || posicion == null) {
      setState(() {
        _targetAltAz = null;
        _isInRange = false;
        _isTracking = false;
      });
      return;
    }
    
    final info = selectedObject!;
    final raDeg = raStringToDegrees(info.ra);
    final decDeg = decStringToDegrees(info.dec);
    final altAz = ecuatorialAHorizontal(
        raDeg: raDeg,
        decDeg: decDeg,
        latDeg: posicion!.latitude,
        lonDeg: posicion!.longitude,
        fechaHora: DateTime.now());
  
    if (_currentTelescopeAzimut != null && _currentTelescopeAltitud != null) {
      double deltaAz = altAz['azimut']! - _currentTelescopeAzimut!;
      if (deltaAz > 180) deltaAz -= 360;
      if (deltaAz < -180) deltaAz += 360;
      
      double deltaAlt = altAz['altitud']! - _currentTelescopeAltitud!;
      
      setState(() {
        _targetAltAz = altAz;
        _deltaAz = deltaAz;
        _deltaAlt = deltaAlt;
        _isInRange = (_deltaAz.abs() < _errorMargin) && (_deltaAlt.abs() < _errorMargin);
        _isTracking = (_isInRange)? _isTracking : false ;
      });
    } else {
      setState(() {
        _targetAltAz = altAz;
        _isInRange = false;
        _isTracking = false;
      });
    }
  }
  
  Future<void> _enviarCoordenadas(Map<String, double> altAz) async {
    if (!isConnected) return;
  
    String mensaje = "${altAz['azimut']!.toStringAsFixed(2)},${altAz['altitud']!.toStringAsFixed(2)}\n";
  
    try {
      await _targetCharacteristic!.write(
        utf8.encode(mensaje),
        withoutResponse: true 
      );
      debugPrint("Paquete de seguimiento enviado: $mensaje");
    } catch (e) {
      debugPrint('Error al enviar en modo tracking: $e');
      if (mounted) setState(() => _isTracking = false);
    }
  }
  
  Future<void> _enviarEstrella() async {
    if (_isTracking) {
      setState(() {
        _isTracking = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Seguimiento detenido')));
      return;
    }
  
    if (selectedObject == null) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('Selecciona una estrella primero')));
      return;
    }
    if (!isConnected) { 
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('No estás conectado al dispositivo')));
      return;
    }
    if (posicion == null) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('Esperando ubicación GPS...')));
      return;
    }
  
    if (!_isInRange) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('Apunta el telescopio primero')));
      return;
    }
  
    if (_targetAltAz != null) {
      await _enviarCoordenadas(_targetAltAz!);
      setState(() {
        _isTracking = true;
        _isstall = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('✓ Seguimiento iniciado')));
    }
  }

  Future<void> _obtenerUbicacion() async {
    try {
      bool serviceEnabled = await Geolocator.isLocationServiceEnabled();
      if (!serviceEnabled) {
        debugPrint('Servicio de ubicación desactivado');
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('Por favor activa la ubicación'))
          );
        }
        return;
      }

      LocationPermission permission = await Geolocator.checkPermission();
      debugPrint('Permiso de ubicación: $permission');
      
      if (permission == LocationPermission.denied) {
        permission = await Geolocator.requestPermission();
        if (permission != LocationPermission.whileInUse &&
            permission != LocationPermission.always) {
          debugPrint('Permiso de ubicación denegado');
          if (mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('Se requiere permiso de ubicación'))
            );
          }
          return;
        }
      }

      debugPrint('Obteniendo posición GPS...');
      Position pos = await Geolocator.getCurrentPosition(
        desiredAccuracy: LocationAccuracy.high,
        timeLimit: const Duration(seconds: 15),
      );
      
      debugPrint('Ubicación obtenida: ${pos.latitude}, ${pos.longitude}');
      
      if (mounted) {
        setState(() => posicion = pos);
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('✓ Ubicación: ${pos.latitude.toStringAsFixed(4)}, ${pos.longitude.toStringAsFixed(4)}'),
            duration: const Duration(seconds: 2),
          )
        );
      }
      _updateGuidance();
    } catch (e) {
      debugPrint('Error al obtener ubicación: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Error GPS: $e'))
        );
      }
    }
  }

  double raStringToDegrees(String ra) {
    final parts = ra.split(RegExp(r'[hms]')).map((e) => e.trim()).where((e) =>
    e.isNotEmpty).toList();
    final h = int.parse(parts[0]);
    final m = int.parse(parts[1]);
    final s = double.parse(parts[2]);
    return (h + m / 60 + s / 3600) * 15.0;
  }

  double decStringToDegrees(String dec) {
    final parts = dec.split(RegExp(r'[°′″]')).map((e) => e.trim()).where((
        e) => e.isNotEmpty).toList();
    final d = int.parse(parts[0]);
    final m = int.parse(parts[1]);
    final s = double.parse(parts[2]);
    return d.sign * (d.abs() + m / 60 + s / 3600);
  }

  Map<String, double> ecuatorialAHorizontal(
      { required double raDeg, required double decDeg, required double latDeg, required double lonDeg, required DateTime fechaHora}) {
    double degToRad(double deg) => deg * pi / 180;
    double radToDeg(double rad) => rad * 180 / pi;
    double ra = degToRad(raDeg),
        dec = degToRad(decDeg),
        lat = degToRad(latDeg);
    double jd = fechaHora
        .toUtc()
        .millisecondsSinceEpoch / 86400000.0 + 2440587.5;
    double d = jd - 2451545.0;
    double gmst = (18.697374558 + 24.06570982441908 * d) % 24;
    double lst = (gmst + lonDeg / 15.0) % 24;
    double lstRad = degToRad(lst * 15);
    double ha = lstRad - ra;
    if (ha < 0) ha += 2 * pi;
    double sinAlt = sin(dec) * sin(lat) + cos(dec) * cos(lat) * cos(ha);
    double alt = asin(sinAlt);
    double cosAz = (sin(dec) - sin(alt) * sin(lat)) / (cos(alt) * cos(lat));
    double az = acos(cosAz);
    if (sin(ha) > 0) az = 2 * pi - az;
    return {'altitud': radToDeg(alt), 'azimut': radToDeg(az)};
  }

  @override
  Widget build(BuildContext context) {
    Widget currentPage;

    if (!_isInitialized) {
      currentPage = _buildLoadingScreen('Iniciando servicios...');
    } else if (isLoadingData) {
      currentPage = _buildLoadingScreen('Cargando objetos celestes...');
    } else {
      currentPage = RefreshIndicator(
        onRefresh: _reintentarConexion,
        child: ListView(
          padding: const EdgeInsets.all(16.0), 
          physics: const AlwaysScrollableScrollPhysics(), 
          children: [
            _buildConnectionStatus(),
            const SizedBox(height: 20),
            _buildObjectSelector(),
            const SizedBox(height: 16),
            
            if (selectedObject != null) ...[
              _buildObjectCard(),
              const SizedBox(height: 16),
            ],
            
            if (isConnected && selectedObject != null) ...[
              _buildGuidanceWidget(),
              const SizedBox(height: 20),
            ],
            
            _buildActionButtons(),
          ],
        ),
      );
    }

    return Scaffold(
      appBar: AppBar(
        title: Row(
          children: [
            Icon(Icons.auto_awesome, color: Colors.amber),
            SizedBox(width: 8),
            Text('Horizonte'),
          ],
        ),
        centerTitle: false,
      ),
      body: currentPage,
    );
  }

  Widget _buildLoadingScreen(String message) {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          CircularProgressIndicator(color: Colors.amber),
          SizedBox(height: 20),
          Text(message, style: TextStyle(fontSize: 16)),
        ],
      ),
    );
  }

  Widget _buildConnectionStatus() {
    return Column(
      children: [
        // Estado Bluetooth
        Container(
          padding: EdgeInsets.all(12),
          decoration: BoxDecoration(
            color: isConnected ? Colors.green.withOpacity(0.1) : Colors.grey.withOpacity(0.1),
            borderRadius: BorderRadius.circular(12),
            border: Border.all(
              color: isConnected ? Colors.green : Colors.grey,
              width: 2,
            ),
          ),
          child: Row(
            children: [
              Icon(
                isConnected ? Icons.bluetooth_connected : Icons.bluetooth_disabled,
                color: isConnected ? Colors.green : Colors.grey,
              ),
              SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      isConnected ? 'Telescopio Conectado' : 'Telescopio Desconectado',
                      style: TextStyle(
                        fontWeight: FontWeight.bold,
                        color: isConnected ? Colors.green : Colors.grey,
                      ),
                    ),
                    if (isConnected && _currentTelescopeAzimut != null)
                      Text(
                        'Az: ${_currentTelescopeAzimut!.toStringAsFixed(1)}° | Alt: ${_currentTelescopeAltitud!.toStringAsFixed(1)}°',
                        style: TextStyle(fontSize: 12, color: Colors.white70),
                      ),
                  ],
                ),
              ),
            ],
          ),
        ),
        
        SizedBox(height: 12),
        
        // Estado GPS
        Container(
          padding: EdgeInsets.all(12),
          decoration: BoxDecoration(
            color: posicion != null ? Colors.blue.withOpacity(0.1) : Colors.orange.withOpacity(0.1),
            borderRadius: BorderRadius.circular(12),
            border: Border.all(
              color: posicion != null ? Colors.blue : Colors.orange,
              width: 2,
            ),
          ),
          child: Row(
            children: [
              Icon(
                posicion != null ? Icons.location_on : Icons.location_off,
                color: posicion != null ? Colors.blue : Colors.orange,
              ),
              SizedBox(width: 12),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      posicion != null ? 'GPS Activo' : 'Obteniendo GPS...',
                      style: TextStyle(
                        fontWeight: FontWeight.bold,
                        color: posicion != null ? Colors.blue : Colors.orange,
                      ),
                    ),
                    if (posicion != null)
                      Text(
                        'Lat: ${posicion!.latitude.toStringAsFixed(4)}° | Lon: ${posicion!.longitude.toStringAsFixed(4)}°',
                        style: TextStyle(fontSize: 12, color: Colors.white70),
                      )
                    else
                      Text(
                        'Esperando señal de ubicación...',
                        style: TextStyle(fontSize: 12, color: Colors.white70),
                      ),
                  ],
                ),
              ),
              if (posicion == null)
                SizedBox(
                  width: 20,
                  height: 20,
                  child: CircularProgressIndicator(
                    strokeWidth: 2,
                    color: Colors.orange,
                  ),
                ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildObjectSelector() {
    return Container(
      padding: EdgeInsets.symmetric(horizontal: 12, vertical: 4),
      decoration: BoxDecoration(
        color: Color(0xFF161B22),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.white24),
      ),
      child: DropdownButton<AstroObject>(
        hint: Row(
          children: [
            Icon(Icons.star_outline, size: 20),
            SizedBox(width: 8),
            Text('Selecciona un objeto celeste'),
          ],
        ),
        value: selectedObject,
        isExpanded: true,
        underline: SizedBox(),
        dropdownColor: Color(0xFF161B22),
        items: astroObjects.map((AstroObject object) =>
            DropdownMenuItem<AstroObject>(
                value: object, 
                child: Text(object.name)
            )
        ).toList(),
        onChanged: (AstroObject? newValue){
          setState(() => selectedObject = newValue);
          _updateGuidance();
        },    
      ),
    );
  }

  Widget _buildObjectCard() {
    return Card(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          if (selectedObject!.imageUrl.isNotEmpty)
            ClipRRect(
              borderRadius: BorderRadius.vertical(top: Radius.circular(16)),
              child: selectedObject!.imageUrl.startsWith('http')
                  ? Image.network(
                selectedObject!.imageUrl,
                height: 200,
                fit: BoxFit.cover,
              )
                  : Image.asset(
                selectedObject!.imageUrl,
                height: 200,
                fit: BoxFit.cover,
              ),
            ),
          
          Padding(
            padding: const EdgeInsets.all(16.0),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Icon(Icons.auto_awesome, color: Colors.amber, size: 20),
                    SizedBox(width: 8),
                    Expanded(
                      child: Text(
                        selectedObject!.name,
                        style: TextStyle(
                          fontSize: 22,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                    ),
                  ],
                ),
                SizedBox(height: 12),
                
                Text(
                  selectedObject!.info,
                  style: TextStyle(color: Colors.white70, height: 1.4),
                ),
                SizedBox(height: 16),
                
                _buildInfoRow(Icons.stars, 'Constelación', selectedObject!.constellation),
                SizedBox(height: 8),
                _buildInfoRow(Icons.my_location, 'Ascensión Recta', selectedObject!.ra),
                SizedBox(height: 8),
                _buildInfoRow(Icons.height, 'Declinación', selectedObject!.dec),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildInfoRow(IconData icon, String label, String value) {
    return Row(
      children: [
        Icon(icon, size: 16, color: Colors.amber),
        SizedBox(width: 8),
        Text('$label: ', style: TextStyle(color: Colors.white70)),
        Expanded(
          child: Text(
            value,
            style: TextStyle(fontWeight: FontWeight.w500),
          ),
        ),
      ],
    );
  }

  Widget _buildGuidanceWidget() {
    if (_currentTelescopeAzimut == null) {
      return Card(
        child: Padding(
          padding: const EdgeInsets.all(20.0),
          child: Column(
            children: [
              CircularProgressIndicator(strokeWidth: 3, color: Colors.amber),
              SizedBox(height: 16),
              Text(
                "Esperando posición del telescopio...",
                style: TextStyle(color: Colors.white70),
              ),
            ],
          ),
        ),
      );
    }

    if (_isInRange) {
      return Card(
        color: Colors.green.withOpacity(0.1),
        child: Padding(
          padding: const EdgeInsets.all(20.0),
          child: Column(
            children: [
              Container(
                padding: EdgeInsets.all(16),
                decoration: BoxDecoration(
                  color: Colors.green.withOpacity(0.2),
                  shape: BoxShape.circle,
                ),
                child: Icon(Icons.check_circle, color: Colors.green, size: 48),
              ),
              SizedBox(height: 16),
              Text(
                "¡Objetivo Centrado!", 
                style: TextStyle(
                  color: Colors.green, 
                  fontSize: 20, 
                  fontWeight: FontWeight.bold
                )
              ),
              SizedBox(height: 8),
              Text(
                "Listo para iniciar seguimiento",
                style: TextStyle(color: Colors.white70),
              ),
            ],
          ),
        ),
      );
    }

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(20.0),
        child: Column(
          children: [
            Text(
              "Ajusta la posición del telescopio",
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w500),
            ),
            SizedBox(height: 24),

            // Flecha ARRIBA
            _buildArrow(
              Icons.keyboard_arrow_up,
              _deltaAlt > _errorMargin,
              'Subir'
            ),

            SizedBox(height: 8),

            // Flechas Laterales
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                _buildArrow(
                  Icons.keyboard_arrow_left,
                  _deltaAz < -_errorMargin,
                  'Izquierda'
                ),
                
                SizedBox(width: 16),
                
                Container(
                  padding: EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: Colors.blue.withOpacity(0.2),
                    shape: BoxShape.circle,
                  ),
                  child: Icon(Icons.gps_fixed, size: 32, color: Colors.blueAccent),
                ),
                
                SizedBox(width: 16),
                
                _buildArrow(
                  Icons.keyboard_arrow_right,
                  _deltaAz > _errorMargin,
                  'Derecha'
                ),
              ],
            ),

            SizedBox(height: 8),

            // Flecha ABAJO
            _buildArrow(
              Icons.keyboard_arrow_down,
              _deltaAlt < -_errorMargin,
              'Bajar'
            ),

            SizedBox(height: 20),
            
            Divider(color: Colors.white24),
            
            SizedBox(height: 12),
            
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceAround,
              children: [
                _buildErrorIndicator(
                  'Altitud',
                  _deltaAlt,
                  Icons.height,
                ),
                Container(
                  width: 1,
                  height: 40,
                  color: Colors.white24,
                ),
                _buildErrorIndicator(
                  'Azimut',
                  _deltaAz,
                  Icons.explore,
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildArrow(IconData icon, bool isActive, String label) {
    return Column(
      children: [
        Icon(
          icon,
          size: 48,
          color: isActive ? Colors.amber : Colors.grey.shade800,
        ),
        if (isActive)
          Text(
            label,
            style: TextStyle(
              fontSize: 10,
              color: Colors.amber,
              fontWeight: FontWeight.bold,
            ),
          ),
      ],
    );
  }

  Widget _buildErrorIndicator(String label, double error, IconData icon) {
    Color color = error.abs() < _errorMargin ? Colors.green : Colors.orange;
    
    return Column(
      children: [
        Icon(icon, size: 20, color: color),
        SizedBox(height: 4),
        Text(
          label,
          style: TextStyle(fontSize: 12, color: Colors.white70),
        ),
        SizedBox(height: 4),
        Text(
          '${error.toStringAsFixed(1)}°',
          style: TextStyle(
            fontSize: 18,
            fontWeight: FontWeight.bold,
            color: color,
          ),
        ),
      ],
    );
  }

  Widget _buildActionButtons() {
    return Column(
      children: [
        // Botón de Conexión
        SizedBox(
          width: double.infinity,
          height: 54,
          child: ElevatedButton.icon(
            onPressed: (_isConnecting || _isDisconnecting) ? null : _conectarODesconectar,
            icon: Icon(
              _isConnecting
                  ? Icons.bluetooth_searching
                  : _isDisconnecting
                      ? Icons.bluetooth_disabled
                      : isConnected
                          ? Icons.bluetooth_connected
                          : Icons.bluetooth,
              size: 24,
            ),
            label: Text(
              _isConnecting
                  ? 'Buscando dispositivo...'
                  : _isDisconnecting
                      ? 'Desconectando...'
                      : isConnected
                          ? 'Desconectar Telescopio'
                          : 'Conectar al Telescopio',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
            ),
            style: ElevatedButton.styleFrom(
              backgroundColor: isConnected 
                  ? Colors.red.shade700 
                  : Colors.blue.shade700,
              foregroundColor: Colors.white,
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(12),
              ),
              elevation: 4,
            ),
          ),
        ),
        
        SizedBox(height: 12),
        
        // Botón de Seguimiento
        SizedBox(
          width: double.infinity,
          height: 54,
          child: ElevatedButton.icon(
            onPressed: (isConnected && selectedObject != null) ? _enviarEstrella : null,
            icon: Icon(
              _isTracking ? Icons.stop : Icons.track_changes,
              size: 24,
            ),
            label: Text(
              _isTracking 
                ? 'Detener Seguimiento' 
                : 'Iniciar Seguimiento',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
            ),
            style: ElevatedButton.styleFrom(
              backgroundColor: _isTracking 
                ? Colors.orange.shade700
                : (_isInRange ? Colors.green.shade700 : Colors.grey.shade700),
              foregroundColor: Colors.white,
              disabledBackgroundColor: Colors.grey.shade800,
              disabledForegroundColor: Colors.grey.shade600,
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(12),
              ),
              elevation: 4,
            ),
          ),
        ),
        
        if (!isConnected || selectedObject == null)
          Padding(
            padding: const EdgeInsets.only(top: 12.0),
            child: Text(
              !isConnected 
                ? '⚠ Conecta el telescopio primero'
                : '⚠ Selecciona un objeto celeste',
              style: TextStyle(
                color: Colors.orange.shade300,
                fontSize: 13,
              ),
            ),
          ),
      ],
    );
  }
}  