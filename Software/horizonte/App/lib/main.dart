import 'package:flutter/material.dart';
import 'package:flutter_bluetooth_serial/flutter_bluetooth_serial.dart';
import 'package:geolocator/geolocator.dart';
import 'package:permission_handler/permission_handler.dart';
import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'dart:typed_data';
import 'package:http/http.dart' as http;
import 'dart:io'; // Para SocketException



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
  runApp(const ExploradorEstelarApp());
}

class ExploradorEstelarApp extends StatelessWidget {
  const ExploradorEstelarApp({super.key});


  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'Explorador Estelar',
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

  BluetoothState _bluetoothState = BluetoothState.UNKNOWN;
  BluetoothConnection? connection;
  bool isConnecting = false;

  bool get isConnected => connection != null && connection!.isConnected;

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

  bool _isInitialized = false; // <-- NUEVA VARIABLE: Para controlar la inicialización
  bool isLoadingData = false;


  @override
  void initState() {

    super.initState();

    WidgetsBinding.instance.addPostFrameCallback((_) {
      _initialize();
    });
  }

  @override
  void dispose() {
    connection?.dispose();
    super.dispose();
  }

  Future<void> _initialize() async {
    await _checkPermissionsAndServices();
    _fetchAstroData();
    if (mounted) {
      setState(() {
        _isInitialized = true;
      });
      // 3. Y SOLO ENTONCES, procedemos a buscar los datos.
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
    FlutterBluetoothSerial.instance.onStateChanged().listen((state) {
      if (mounted) setState(() => _bluetoothState = state);
    });
    debugPrint("Paso 2/7: Listener de Bluetooth configurado.");
    debugPrint("Paso 3/7: Verificando si la ubicación está activada...");
    bool isLocationEnabled = await Geolocator.isLocationServiceEnabled();
    debugPrint("Paso 4/7: Verificación de ubicación completa. ¿Activada?: $isLocationEnabled");
    debugPrint("Paso 5/7: Verificando estado del Bluetooth...");
    _bluetoothState = await FlutterBluetoothSerial.instance.state;
    debugPrint("Paso 6/7: Verificación de Bluetooth completa. Estado: $_bluetoothState");
    if (_bluetoothState == BluetoothState.STATE_OFF) {
      if (mounted) await _showServiceDialog(
          "Activar Bluetooth", "Para continuar, por favor activa el Bluetooth.",
          FlutterBluetoothSerial.instance.openSettings);

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
      barrierDismissible: false, // El usuario debe tomar una acción
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
                // Opcional: podrías cerrar la app si los servicios son obligatorios
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

  // --- LÓGICA DE CONEXIÓN MEJORADA ---
  Future<void> _conectarODesconectar() async {
    if (isConnected) {
      await connection?.close();
      setState(() {});
    } else {
      // Si el bluetooth NO está encendido, avisar al usuario
      if (_bluetoothState != BluetoothState.STATE_ON) {
        ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text(
                'Por favor, activa el Bluetooth para poder conectar.'))
        );
        return; // Salir de la función para no intentar buscar
      }
      _buscarYConectar();
    }
  }

  void _buscarYConectar() async {
    setState(() {
      isConnecting = true;
    });
    try {
      BluetoothDevice? hc06;
      // Usamos un Completer para esperar el resultado del Stream
      final completer = Completer<BluetoothDevice?>();

      StreamSubscription<
          BluetoothDiscoveryResult> streamSubscription = FlutterBluetoothSerial
          .instance.startDiscovery().listen((r) {
        if (r.device.name == "HC-06" && !completer.isCompleted) {
          completer.complete(r.device);
        }
      });

      // Esperar un máximo de 10 segundos
      hc06 = await completer.future.timeout(
          const Duration(seconds: 10), onTimeout: () => null);

      streamSubscription.cancel(); // Siempre cancelar el stream

      if (hc06 != null) {
        await _conectarAlDispositivo(hc06);
      } else {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(const SnackBar(
              content: Text('No se encontró el dispositivo HC-06.')));
        }
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('Error al buscar: $e')));
      }
    } finally {
      if (mounted) setState(() {
        isConnecting = false;
      });
    }
  }

  Future<void> _conectarAlDispositivo(BluetoothDevice server) async {
    try {
      connection = await BluetoothConnection.toAddress(server.address);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('Conectado a ${server.name}')));
      }
      setState(() {}); // Actualiza la UI
      connection!.input!.listen(null).onDone(() {
        if (mounted) setState(() {}); // Se desconectó, actualiza la UI
      });
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text('Error al conectar: $e')));
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
          content: Text('No estás conectado al dispositivo Bluetooth.')));
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
      fechaHora: DateTime.now(),
    );

    String mensaje = "${altAz['azimut']!.toStringAsFixed(2)},${altAz['altitud']!
        .toStringAsFixed(2)}\n";

    try {
      connection!.output.add(Uint8List.fromList(utf8.encode(mensaje)));
      await connection!.output.allSent;
      ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Enviado: $mensaje')));
    } catch (e) {
      ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Error al enviar: $e')));
    }
  }

  Future<void> _checkPermissions() async {
    await [
      Permission.location,
      Permission.bluetooth,
      Permission.bluetoothScan,
      Permission.bluetoothConnect
    ].request();
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

            const Spacer(), // Empuja los botones hacia abajo

            ElevatedButton(
              onPressed: isConnecting ? null : _conectarODesconectar,
              child: Text(isConnecting ? 'Buscando...' : (isConnected
                  ? 'Desconectar'
                  : 'Conectar a HC-06')),
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
      appBar: AppBar(title: const Text('Explorador Estelar')),
      body: Center(child: currentPage),
    );
  }
}
 