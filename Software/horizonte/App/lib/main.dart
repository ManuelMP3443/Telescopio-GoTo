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
      theme: ThemeData.dark(),
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

  bool _isConnecting = false;
  bool _isDisconnecting = false; 
  bool get isConnected => _targetCharacteristic != null;

  // --- DEFINE TUS UUIDS AQUÍ ---
  // Deben coincidir exactamente con los del firmware de tu dispositivo BLE
  final Guid SERVICE_UUID = Guid("0000FFE0-0000-1000-8000-00805F9B34FB");
  final Guid CHARACTERISTIC_UUID = Guid("0000FFE2-0000-1000-8000-00805F9B34FB");


  Position? posicion;
  //datos locales
  List<AstroObject> astroObjects = [
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

  AstroObject? selectedObject;
  bool isLoading = true;

  bool _isInitialized = false; 
  bool isLoadingData = false;


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
  }

  @override
  void dispose() {
    _adapterStateSubscription?.cancel();
    _deviceStateSubscription?.cancel();
    _targetDevice?.disconnect();
    super.dispose();
  }

  Future<void> _initialize() async {
    await _checkPermissionsAndServices();
    _fetchAstroData();
    if (mounted) {
      setState(() {
        _isInitialized = true;
      });
      _fetchAstroData();
    }
  }

  // --- NUEVA LÓGICA DE INICIO ---
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

  // --- LÓGICA DE CONEXIÓN BLE ---
  Future<void> _conectarODesconectar() async {
  if (isConnected) {
    if (_isDisconnecting) return; 

    setState(() {
      _isDisconnecting = true;
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
    // ---- Lógica de Conexión ----
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

    // --- SECUENCIA DE CONEXIÓN ---
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
                });
            }
        }
    });

    // PASO CLAVE 1: Conectar usando la variable LOCAL 'foundDevice'
    await foundDevice!.connect(autoConnect: false);
    print("Paso 1/2: Conexión física establecida.");

    // PASO CLAVE 2: Descubrir servicios usando la variable LOCAL 'foundDevice'
    List<BluetoothService> services = await foundDevice!.discoverServices();
    print("Paso 2/2: Servicios descubiertos con éxito.");

    for (var service in services) {
      if (service.uuid == SERVICE_UUID) {
        for (var characteristic in service.characteristics) {
          if (characteristic.uuid == CHARACTERISTIC_UUID) {
            print("¡ÉXITO TOTAL! Característica encontrada. La app está lista.");
            if (mounted) {
              ScaffoldMessenger.of(context).showSnackBar(
                const SnackBar(content: Text('Conectado y listo para usar.'))
              );
              // PASO CLAVE 3: SOLO AHORA, al final y con éxito, actualizamos el estado de la clase.
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
      // Nos aseguramos de desconectar si algo falló.
      foundDevice?.disconnect();
      setState(() => _isConnecting = false);
    }
  }
}


  Future<void> _enviarEstrella() async {
    if (selectedObject == null) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('Por favor, selecciona una estrella primero.')));
      return;
    }
    if (!isConnected) { 
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('No estás conectado al dispositivo BLE.')));
      return;
    }
    if (posicion == null) {
      ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
          content: Text('Esperando obtener la ubicación GPS...')));
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

    String mensaje = "${altAz['azimut']!.toStringAsFixed(2)},${altAz['altitud']!.toStringAsFixed(2)}\n";

    try {
      // Escribir el valor en la característica BLE
      await _targetCharacteristic!.write(
        utf8.encode(mensaje), // Convertir el String a List<int>
        withoutResponse: true 
      );

      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text('Enviado: $mensaje')));
    } catch (e) {
      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text('Error al enviar: $e')));
    }
  }


  Future<void> _obtenerUbicacion() async {
    try {
      bool serviceEnabled = await Geolocator.isLocationServiceEnabled();
      if (!serviceEnabled) return;

      LocationPermission permission = await Geolocator.checkPermission();
      if (permission == LocationPermission.denied) {
        permission = await Geolocator.requestPermission();
        if (permission != LocationPermission.whileInUse &&
            permission != LocationPermission.always) return;
      }

      Position pos = await Geolocator.getCurrentPosition(
          desiredAccuracy: LocationAccuracy.high);
      if (mounted) setState(() => posicion = pos);
    } catch (e) {
      debugPrint('Error al obtener ubicación: $e');
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
      // Estado 1: Inicializando permisos y servicios
      currentPage = const Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          CircularProgressIndicator(),
          SizedBox(height: 16),
          Text('Iniciando servicios...'),
        ],
      );
    } else if (isLoadingData) {
      // Estado 2: Servicios listos, ahora cargando datos del servidor
      currentPage = const Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          CircularProgressIndicator(),
          SizedBox(height: 16),
          Text('Cargando datos de las estrellas...'),
        ],
      );
    } else {

      currentPage = Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            DropdownButton<AstroObject>(
              hint: const Text('Selecciona un objeto celeste'),
              value: selectedObject,
              isExpanded: true,
              items: astroObjects.map((AstroObject object) =>
                  DropdownMenuItem<AstroObject>(
                      value: object, child: Text(object.name))).toList(),
              onChanged: (AstroObject? newValue) =>
                  setState(() => selectedObject = newValue),
            ),
            const SizedBox(height: 20),

            // --- ESTE ES EL BLOQUE QUE MUESTRA LA INFORMACIÓN ---
            // Solo se construye si 'selectedObject' no es nulo.
            if (selectedObject != null)
              Card(
                color: Colors.black87,
                margin: const EdgeInsets.all(12),
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                child: Padding(
                  padding: const EdgeInsets.all(12.0),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      // Imagen del astro
                      if (selectedObject!.imageUrl.isNotEmpty)
                        ClipRRect(
                          borderRadius: BorderRadius.circular(12),
                          child: selectedObject!.imageUrl.startsWith('http')
                              ? Image.network(
                            selectedObject!.imageUrl,
                            height: 180,
                            fit: BoxFit.cover,
                          )
                              : Image.asset(
                            selectedObject!.imageUrl,
                            height: 180,
                            fit: BoxFit.cover,
                          ),
                        ),
                      const SizedBox(height: 12),

                      // Nombre
                      Text(
                        selectedObject!.name,
                        style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                          color: Colors.white,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 8),

                      // Información
                      Text(
                        selectedObject!.info,
                        style: const TextStyle(color: Colors.white70),
                      ),
                      const SizedBox(height: 8),

                      // Otros datos
                      Text(
                        'Constelación: ${selectedObject!.constellation}',
                        style: const TextStyle(color: Colors.white70),
                      ),
                      Text(
                        'RA: ${selectedObject!.ra}',
                        style: const TextStyle(color: Colors.white70),
                      ),
                      Text(
                        'Dec: ${selectedObject!.dec}',
                        style: const TextStyle(color: Colors.white70),
                      ),
                    ],
                  ),
                ),
              ),

            const Spacer(),

            ElevatedButton(
              // Deshabilitar el botón si está conectando O desconectando.
              onPressed: (_isConnecting || _isDisconnecting) ? null : _conectarODesconectar,

              // Cambiar el texto del botón según el estado actual.
              child: Text(
                _isConnecting
                  ? 'Buscando...'
                  : _isDisconnecting
                    ? 'Desconectando...'
                    : isConnected
                      ? 'Desconectar'
                      : 'Conectar al Telescopio'
              ),
              style: ElevatedButton.styleFrom(
                // Opcional: Cambiar el color para que sea más claro
                backgroundColor: isConnected ? Colors.redAccent : Colors.blueAccent,
              ),
            ),
            const SizedBox(height: 10),
            ElevatedButton(
              onPressed: _enviarEstrella,
              child: const Text('Apuntar Telescopio'),
            ),
          ],
        ),
      );
    }

    return Scaffold(
      appBar: AppBar(title: const Text('Horizonte')),
      body: Center(child: currentPage),
    );
  }
}
 