================================================================================
  SALARINI X  -  V28 MERGED (DUAL AIR PAIR - EMPAREJAMIENTO POR EL AIRE +
  EVIL TWIN CON POPUP CAUTIVO EN TODOS LOS MOVILES + BOTONES PAGE UP/DOWN
  ELIMINADOS + SCROLL DE RAIZ + SMART ATTACK + FIND WIFI + AUTO-PIN)
  UI grafica completa de SALARINI + motor de ataques real de NOVA X
  + SMART ATTACK: un toque = escaneo + handshake + evil twin automaticos
  + MAC RANDOM / MAC RESTORE + drone v1.6 con arranque blindado (fix
  CPU_LOCKUP) + pager visible en todos los menus + DUAL MODE + DUO
  + SWEEP JAM + BEACON CLONE + TX sin limite legal + MEGA EVIL TWIN
================================================================================

  !! AVISO LEGAL !!
  Este firmware transmite tramas Wi-Fi y BLE de ataque REALES.
  Usalo SOLO en redes que sean tuyas o para las que tengas autorizacion
  por escrito (pentesting, laboratorios, formacion). Atacar redes ajenas
  es un DELITO en la mayoria de paises (CFAA/FCC en EE.UU., GDPR y leyes
  de telecomunicaciones en la UE). Los autores no se hacen responsables
  del mal uso. Practica siempre en tu propio laboratorio.

--------------------------------------------------------------------------------
NOVEDADES V28 - LO QUE PEDISTE AHORA
--------------------------------------------------------------------------------
* BOTONES PAGE UP / PAGE DOWN ELIMINADOS:
  - Fuera los dos botones que molestan. Con el scroll de V27 funcionando en
    TODOS los menus (deslizar el dedo arriba/abajo), los botones sobraban.
  - Se queda solo la barra lateral fina como indicador. Un gesto, un idioma,
    en todas las listas: ataques, WiFi, stations, SD, HELP, portales.

* DUAL AIR PAIR - EL DRONE ESCUCHA LAS SOLICITUDES TODO EL TIEMPO:
  - Enciendes DUAL MODE en Settings y la placa principal MANDA UNA SOLICITUD
    POR EL AIRE (marcadores de radio en el canal 1, ~12 s).
  - El SX-DRONE (v1.8) esta ESCUCHANDO SOLICITUDES SIEMPRE que esta libre:
    recibe la peticion, responde con SU codigo y queda GUARDADO en las dos
    placas. Primera vez = descubrimiento (responde cualquier drone); desde
    entonces la peticion lleva el codigo guardado = SOLO TU drone responde
    ("hasta conseguir la correcta").
  - Al confirmarse el emparejamiento: popup "DRONE PAIRED" y arranca el
    enlace de 3 cables AUTO-PIN (que ya no depende de acertar pines).
  - MUY RAPIDO y SIN PERDER RENDIMIENTO: el oyente del drone se apaga solo
    durante los ataques (cero coste de ataque) y no lleva pila Bluetooth
    (BLE se comia ~300 KB de flash y tiempo de radio; esto es radio directa,
    va por la misma antena 2.4 GHz y tarda ~2 s).

* EVIL TWIN: POPUP CAUTIVO AUTOMATICO EN CUALQUIER DISPOSITIVO:
  - Windows (NCSI/msftconnecttest), Android (generate_204 de Google, MIUI,
    EMUI, vivo), iPhone/iOS/Mac (hotspot-detect de Apple), Kindle y Firefox
    (detectportal): TODOS los sondeos de conectividad reciben un 302
    INSTANTANEO - el movil abre la ventana del portal EL SOLO en 1-3 s
    desde que se conecta al WiFi del ataque.
  - Cualquier URL desconocida tambien redirige ahora (antes algunas paginas
    devolvian 200 y el movil creia que "hay internet" y no sacaba el popup).
  - Aplica a TODOS los evil twins: portal normal, clonado, MEGA y el del
    SMART ATTACK.

--------------------------------------------------------------------------------
NOVEDADES V27 - EL SCROLL DE LOS MENUS, ARREGLADO DE RAIZ (COMO EL MENU WIFI)
--------------------------------------------------------------------------------
* POR QUE NO PODIAS BAJAR EN LOS MENUS DE ATAQUES - FIX DE RAIZ:
  - El bug real: en CADA repintado, los menus obligaban a la lista a volver
    a la fila del cursor (la fila 0). Tu dedo movia la lista, pero el
    siguiente repintado la devolvia arriba = "no lo puedo bajar". Por eso
    el menu de WiFi SI scrolleaba (su lista no tiene cursor) y los menus
    de ataques (DEAUTH, COMBO, B.T ADV, DRONE...) parecian muertos.
  - Ahora la vista solo sigue al cursor cuando el cursor se mueve de
    verdad; un swipe mueve la lista Y SE MANTIENE donde la dejas, con el
    mismo comportamiento que la lista de WiFi.
* SCROLL MAS FIJO Y MAS SEGURO EN LAS LISTAS:
  - Ventana de toque reducida a 14 px en las listas: un arrastre corto ya
    NO dispara el ataque/entrada que estaba debajo del dedo.
  - Un flick vertical pequeno (desde 15 px) ya hace scroll (antes hacia
    falta pasar de 26 px). 1 fila por cada 40 px de recorrido.
  - Funciona en TODOS los menus con scroll: DEAUTH (9), ADVANCED (8/9),
    B.T ADV (9), DRONE DUO (9), IN-NET, HELP (64 temas), portales, hosts
    LAN, ficheros SD, APs y stations. Las pildoras PAGE UP / PAGE DOWN y
    la barra lateral ahora si responden y se mueven con la lista.

--------------------------------------------------------------------------------
NOVEDADES V26 - LO QUE PEDISTE AHORA
--------------------------------------------------------------------------------
* DRONE V1.6 - ADIOS AL BOOTLOOP (rst:0x1a CPU_LOCKUP):
  - El arranque v1.5 llamaba esp_wifi_set_band_mode() DESPUES de arrancar el
    driver; en el C5 eso solo esta definido ANTES de esp_wifi_start() y en
    algunas placas tiraba el CPU entero en bucle. Ahora el orden es el seguro:
    STA -> esp_wifi_stop() -> banda AUTO -> esp_wifi_start().
  - Cada etapa del arranque imprime "[BOOT n] ..." y el motivo del ultimo
    reset. Si una placa siguiera loopeando: Tools > Erase Flash + re-subir,
    y Tools > PSRAM = Disabled (el drone no necesita PSRAM).
  - MODO SEGURO: mantén pulsado el boton BOOT (GPIO9) al alimentar: el drone
    arranca SIN tocar la radio (enlace UART + consola USB funcionan igual).

* SMART ATTACK (Combo > Smart Attack) - UN SOLO TOQUE:
  - Escanea si hace falta, elige la red con nombre MAS FUERTE, captura el
    handshake (max 45 s) y ABRIR EL EVIL TWIN CLON SOLO, con su portal.
  - Con DUAL MODE activado el drone se suma y martillea la red victima.

* MAC SPOOF (AP Spoofing > MAC Random / MAC Restore):
  - Identidad nueva aleatoria para la placa (los logs y listas de clientes
    ven otro dispositivo) y vuelta a la MAC de fabrica cuando quieras.
  - Se bloquea mientras un ataque/escaneo/evil twin usa la radio.

* SCROLL ARREGLADO DE VERDAD EN LOS MENUS DE ATAQUES:
  - El bug: un gesto de 30-45 px (o un poco diagonal) no era ni tap ni
    scroll = zona muerta, "no puedo bajar". Ahora CUALQUIER gesto vertical
    de mas de 26 px desplaza la lista (1 fila por cada 40 px).
  - Ademas BOTONES VISIBLES "PAGE UP / PAGE DOWN" en TODOS los menus que
    sobredimensionan (Deauth 9, ADVANCED 9, B.T ADV 9, DRONE 9, HOW TO USE).

* REPASO GRAFICO COMPLETO: 10 limites de texto reajustados (strip del drone,
  paneles de handshake/Find WiFi/LAN, portal y SD) para que ninguna linea
  pueda rozar o cortarse por el borde derecho de la pantalla.

* ENLACE: el barrido AUTO-PIN tambien rota DENTRO de la ventana de conexion
  (antes solo rotaba entre loops) = primer enlace mas rapido.

--------------------------------------------------------------------------------
NOVEDADES V25 - LO QUE PEDISTE
--------------------------------------------------------------------------------
* ENLACE DUO QUE SE CONFIGURA SOLO (AUTO-PIN):
  - Se acabo el "esta bien cableado pero no conecta". YA NO HAY PINES
    FIJOS QUE ACERTAR: conecta CUALESQUIERA dos GPIO de la lista segura
    (pantalla: 11,12,15,16,17,18,20,21,22,4 / drone: 16,15,17,18,20,21,
    22,4,11,12) + GND, enciende ambas placas y listo.
  - El firmware PANTALLA barre sus pines TX y rota los RX a la vez mandando
    PINGs; el DRONE rota sus RX escuchando, y cuando oye un PING cicla sus
    TX con PONGs hasta que la pantalla le dice ACK. Cada placa GUARDA el
    par encontrado en memoria (NVS): los arranques siguientes enlazan en
    menos de 1 segundo. Si mueves los cables, se vuelve a buscar solo.
  - Si un ataque arranca y el drone aun no aparecio, el ataque empieza
    SOLO y el drone SE SUMA solo a mitad de ataque (repite el trabajo).
  - DUAL MODE sigue funcionando igual (Settings > DUAL MODE, guardado).

* NUEVO ATAQUE: FIND WIFI (DEAUTH > Find WiFi 2.4G / Find WiFi 5G):
  - Eliges la banda, seleccionas UN dispositivo de la lista de estaciones
    y el aparato lo patea SIN PARAR mientras el sniffer mira a QUE red
    vuelve a conectarse.
  - En pantalla aparecen las redes candidatas (canal + MAC + nombre cuando
    el beacon/probe lo revela, incluidas redes OCULTAS). Toca YES en la
    que sea ("ese es") y para el ataque, o toca la red para PASAR a la
    siguiente. Hasta que tu decidas, como pediste.

* CAPTURE HANDSHAKE ARREGLADO (2 bugs de verdad):
  - Los EAPOL de hoy llegan en tramas QoS (2 bytes extra de cabecera) y el
    sniffer solo miraba el offset fijo de tramas normales: por eso nunca
    veia M1-M4. Ahora entiende normal + QoS + QoS+HTC.
  - El canal se re-fijaba ~300 VECES POR SEGUNDO dentro del bucle, lo que
    ahogaba el receptor: no entraba ni una trama. Ahora se fija una vez y
    solo se re-fija si la radio se mueve.
  - Bonus: el PMKID ahora busca el KDE en la zona REAL del key-data.

* SCAN STATIONS MEJORADO ("van y desaparecen"):
  - Las estaciones YA NO se borran al re-escanear (antes cada scan limpiaba
    la lista) y el timeout sube de 60 s a 4 minutos.
  - Lista llena: se expulsa la entrada MAS VIEJA en vez de ignorar a los
    clientes nuevos (la otra causa de "no aparecen").
  - Cada fila muestra dBm + edad ("8s") y se ordena por señal al abrir.
  - Boton CLEAR manual en la cabecera de la lista.

* MENUS CON SCROLL EN TODAS PARTES:
  - Barra de scroll visible a la derecha en menus y listas (DEAUTH ahora
    tiene 9 entradas, DRONE 9, B.T 9...). Arrastrar mueve varias filas de
    una vez; ya no hay opciones "que se salen de la pantalla".

* LETRA W ARREGLADA: los glifos W/w tocaban los dos bordes de su celda y
  el muestreo les recortaba los lados. Ahora se dibujan con el borde
  anclado: los dos palos salen enteros ("HOW TO USE" ya no se ve cortado).

* FLASH: igual que V24.2. Drone sin BLE (enlace UART) -> cabe en la
  particion por defecto. Core Debug Level: None recomendado.

--------------------------------------------------------------------------------
NOVEDADES V24.2 - NUEVOS PINES DEL ENLACE + PANTALLA ARREGLADA
--------------------------------------------------------------------------------
* POR QUE NO CONECTABA AUN CON EL CABLE BIEN PUESTO (V24.1):
  - En la mayoria de placas C5 "normales" (DevKitC-1, C5-Zero, genericas),
    GPIO11 y GPIO12 son el UART0 y van SOLDADOS AL CHIP PUENTE USB-UART de
    la placa (y GPIO13/14 son el USB nativo D-/D+). Ese chip pelea con tu
    cable en la pata RX del drone: parece "todo bien puesto" pero el
    enlace nunca sube. Era eso, no tu cableado.

* NUEVO ESQUEMA (pinos DISTINTOS por placa, a proposito):
      SALARINI X (pantalla)          SX-DRONE (segundo C5)
      GPIO11 (TX)  ---------------->  GPIO16 (RX)
      GPIO12 (RX)  <----------------  GPIO15 (TX)
      GND  <---------------------------> GND
  - La pantalla conserva 11/12: en la Waveshare Touch-LCD-3.5 son la
    cabecera "UART" que expone la placa (libres de fabrica).
  - El drone pasa a 15/16: GPIO normales en cualquier placa C5 (ni puente
    USB, ni USB nativo, ni pines de arranque). Si tu placa no tiene 15/16,
    cambia los dos #define de la cabecera del sketch del drone.
  - Vuelve a cablear con el esquema nuevo y flashea AMBAS placas con esta
    version (el monitor serie del drone debe decir "SX-DRONE v1.4").

* DIAGNOSTICO DE CABLEADO EN PANTALLA (nuevo):
  - Con el enlace caido, el menu DRONE DUO muestra "RX n B HEARD - CHECK
    3 WIRES": RX 0 B = no llega nada (cable/alimentacion/pines); bytes sin
    LINKED = datos corruptos (firmware viejo en alguna placa o el puente
    USB aun sujetando el pin). Se acabó adivinar.
  - El drone manda su latido STAT cada 0.5 s (antes 1 s) y se anuncia el
    solo al arrancar; ambas patas RX llevan pull-up interno (nada de ruido
    con el cable suelto).

* PANTALLA ARREGLADA (lo del texto que se salia):
  - La barra de señal de la lista de redes YA NO ROZA el texto: la fila se
    reordeno en columnas fijas (señal | barras | canal | banda | candado).
  - HOW TO USE: las lineas largas ahora SE PARTEN en dos dentro de la
    misma tarjeta en vez de salirse de la pantalla/menu; titulos y
    subtitulos limitados al ancho real de la tarjeta. Nada se corta ya.

--------------------------------------------------------------------------------
NOVEDADES V24.1 - EL ENLACE DEL DRONE AHORA ES POR CABLE (TX/RX)
--------------------------------------------------------------------------------
* POR QUE EL CAMBIO:
  - El enlace BLE de V24 no conectaba en la practica: la placa decia
    "LINK FAILED" y el drone se quedaba "waiting". Bluetooth entre dos
    C5 depende de versiones de NimBLE, direcciones y publicidad: demasiado
    fragil. V24.1 lo sustituye por un UART de 3 hilos: cero emparejamiento,
    cero escaneo, cero versiones de librerias, cero antenas.

* CABLEADO (3 cables duplex, se cruzan):
      SALARINI X (pantalla)          SX-DRONE (segundo C5)
      GPIO11 (TX)  ---------------->  GPIO12 (RX)
      GPIO12 (RX)  <----------------  GPIO11 (TX)
      GND  <---------------------------> GND
  - En la Waveshare ESP32-C5-Touch-LCD-3.5 ese par es la interfaz "UART"
    que expone la placa. El monitor serie USB de las DOS placas sigue
    funcionando mientras el enlace esta arriba.
  - 115200 baudios, texto plano. El drone manda un latido STAT cada
    segundo: si ese latido llega, el enlace esta UP al instante.

* MAS FIJO QUE NUNCA:
  - El drone responde en milisegundos (es un cable): sin rango, sin
    interferencias, sin "esperando conexion".
  - AUTO-RELINK igual que en V24: desenchufa el drone en pleno ataque y
    al reconectar el cable reenvia solo TODO el trabajo (objetivos, ritmo,
    sweep y beacons clone).
  - El radio BLE de la placa queda 100% libre: el spam B.T ADV puede
    funcionar A LA VEZ que el drone (antes se bloqueaban).

* DRONE v1.3 MAS LIGERO:
  - El firmware del drone YA NO usa NimBLE: sin librerias externas y
    vuelve a caber en la particion POR DEFECTO de 4 MB (se acabo el
    "Sketch too big 106%" y el Huge APP obligatorio).
  - Los mismos tres trabajos de radio: JAM + SWEEP + BEACON CLONE, con
    latido STAT cada segundo (modo, canal, tramas/s, temperatura, TX).
  - Prueba rapida: abre el monitor serie del drone (115200) y veras el
    STAT cada segundo; puedes teclear comandos a mano (PING, STAT...).

--------------------------------------------------------------------------------
NOVEDADES V24 - DUO SWEEP JAM + DUO BEACON CLONE + AUTO-RELINK + FIX BLE
--------------------------------------------------------------------------------
* ARREGLO DE COMPILACION (importante):
  - "error: 'getInitialized' is not a member of 'NimBLEDevice'": algunas
    versiones de NimBLE-Arduino no tienen ese metodo. Eliminado: ahora
    init() se llama siempre (es tolerante a doble init en TODAS las
    versiones) y la libreria lleva bandera propia. Compila con 1.4.x y 2.x.

* NUEVO ATAQUE - DUO SWEEP JAM (menu Drone Duo, sin elegir objetivo):
  - El drone BARRE TODO el espectro (canales 1-13 y los primarios 5 GHz
    36/44/149/157/165) lanzando rafagas de deauth en difusion en cada
    canal, mientras esta placa hace su propia tormenta por canales sobre
    todos los APs conocidos. Dos radios = borrado total de ambas bandas.

* NUEVO ATAQUE - DUO BEACON CLONE (menu Drone Duo, elegir un AP):
  - Esta placa emite beacons del SSID y el drone ADEMAS: 1) JAMea todos
    los BSSIDs reales de esa red (2.4 y 5 GHz) y 2) emite beacons CLONE
    del mismo SSID en un canal lejano (primero un canal 5 GHz de la
    familia, si no un canal 2.4 vecino) copiando el candado de seguridad
    (red WPA se ve WPA). El nombre de la red aparece EN TODAS LAS LISTAS
    de los moviles cercanos gracias a las dos radios a la vez.

* EVIL TWIN REFORZADO (automatico):
  - En cuanto el drone empieza el JAM de la familia victima, tambien emite
    beacons clone del SSID del portal en un canal remoto (con la seguridad
    de la victima espejada). Los clientes en itinerancia ven el nombre en
    todas partes y acaban entrando en el portal real.

* AUTO-RELINK EN PLENO ATAQUE (a prueba de reinicios):
  - El enlace guarda una COPIA del trabajo del drone (objetivos + ritmo +
    sweep + beacon). Si el drone se apaga, se reinicia o se sale de rango
    en mitad de un ataque, la placa principal lo re-enlaza sola (reintentos
    infinitos durante el ataque) y le REENVIA el trabajo completo: sigue
    atacando sin tocar nada.
  - Los ataques del menu DUO y los evil twins liberan ahora el enlace al
    terminar (enlaces automaticos), igual que DUAL MODE.

* TELEMETRIA DEL DRONE (mejorada):
  - STAT nuevo: ST (modo: IDLE/JAM/SWEEP/BCON/COMBO), CH, TG, TX, RPS
    (tramas por segundo), T (temperatura del chip en C) y U (uptime).
  - La franja del menu Drone Duo muestra ahora: "JAM CH6 88F/S 42C".
  - Drone Status muestra modo, canal, tramas/s, temperatura y total TX.

* PROTOCOLO DEL DRONE v1.2 (tambien desde una app BLE de movil):
  - SW / SW:<1|2|3>  -> barrido total del espectro (nuevo)
  - BC:<ch>:<priv>:<hex> -> beacons clone del SSID (hex) en <ch> (nuevo)
  - BCSTOP -> para los beacons clone (nuevo)
  - STOP -> para TODO (jam + sweep + beacons)
  - STAT/INFO ampliados; PING responde v1.2.

--------------------------------------------------------------------------------
NOVEDADES V23.2 - DUAL MODE: UN INTERRUPTOR, TODOS LOS ATAQUES
--------------------------------------------------------------------------------
* SETTINGS > DUAL MODE (nueva fila, ON/OFF, SE GUARDA en la configuracion
  y sobrevive a apagados, como ATTACK POWER):
  - ON  = CADA ataque Wi-Fi enlaza el drone SOLO al empezar, le pasa los
    objetivos y lo para al terminar. No hace falta entrar en el menu Duo.
  - OFF = todo funciona como antes en una sola placa.
  - El estado se ve en el menu Drone Duo ("DUAL MODE ON" en la franja de
    estado) y en el arranque por serie ([DUAL] mode ON/OFF).

* QUE ATAQUES SE VUELVEN DOBLES CON DUAL MODE ON (todos los de radio Wi-Fi):
  - Deauth ALL / canal / seleccionado / STAs, AUTH, ASSOC, beacons (duplic,
    random, prefix, rickroll), EAPOL flood, QoS-Null flood, RTS flood,
    DEAUTH 2-WAY, PROBE REPLY FLOOD, DEAUTH STORM, TOTAL OUTAGE,
    HANDSHAKE CAPTURE (el drone martilla mientras esta placa olfatea
    EAPOL: capturas mucho mas rapidas), PMKID, CLIENT HUNTER,
    PROBE KARMA y TODOS los Evil Twins (portal + drone a la vez).
  - Objetivos: los APs seleccionados van a las DOS radios. Si un cliente
    (STA) no tiene AP conocido, el drone JAMea todo su canal.
  - Ataques sin objetivo fijo (karma, storm, floods): el drone barre con
    JAM de difusion los canales comunes 1/6/11/36/149.
  - El ritmo del drone sigue ATTACK POWER: LOW=GO:1, MED=GO:2, MAX=GO:3.
  - Sin objetivo alcanzable o drone apagado: popup "ATTACK RUNS SOLO" y
    el ataque sigue en una placa - NUNCA se bloquea el ataque.
  - BLE spam queda local (esa radio BLE es la que spamea) y las
    herramientas pasivas (escaneos, monitor) no llevan drone.

* MEJORAS EXTRA:
  - Los ataques del menu DUO (Crossfire/Pincer/Handshake) siguen con su
    coreografia propia (no se duplican objetivos).
  - Evil Twin con DUAL ON: el drone se enlaza automaticamente al lanzar
    el portal y se libera al parar (exito o salida).
  - Ayuda nueva en HOW TO USE: tema "DUAL MODE". About: "DUAL MODE +
    DUO DRONE".

--------------------------------------------------------------------------------
NOVEDADES V23.1 - TX UNLEASHED + ARREGLO DE FLASH DEL DRONE
--------------------------------------------------------------------------------
* POTENCIA DE TRANSMISION SIN TOPE LEGAL EN LAS DOS PLACAS (lo pedido):
  - Wi-Fi: esp_wifi_set_max_tx_power(84) = peticion de 21 dBm = el maximo
    ABSOLUTO que acepta el driver (el chip C5 se queda en su techo de
    fabrica, ~20 dBm en 2.4 GHz). NINGUN limite legal/regulatorio aplicado.
  - Se reafirma en: arranque de la placa, inicio de CADA ataque, inicio de
    cada softAP (evil twin) - y en el drone: arranque, comando GO y cada
    cambio de canal.
  - BLE: ESP_PWR_LVL_P20 (+20 dBm, nivel maximo) en las dos placas.
  - El puerto serie imprime "[RF] TX UNLEASHED: chip applied max NN/4 dBm
    (NO legal cap)" para que veas la potencia que el chip acepto.

* ERROR "SKETCH TOO BIG (106%)" AL COMPILAR EL DRONE - EXPLICACION:
  NO es tu codigo. Las ~400 lineas del drone solo ocupan ~15 KB; el resto
  (1.3 MB) son las pilas BLE (NimBLE) + WiFi/lwIP PRECOMPILADAS del core.
  Ningun cambio de codigo las mete en 1.25 MB. La solucion es UN menu:
    Arduino IDE > Tools > Partition Scheme > "Huge APP (3MB No OTA/1MB SPIFFS)"
  Con esa particion entra con el doble de margen. (Seccion completa en
  README_DRONE.txt y DUO_README.txt.)

* Drone firmware ahora v1.1 (ademas: no re-sintoniza el canal si ya esta
  en el, menos tiempo muerto entre rafagas).

--------------------------------------------------------------------------------
NOVEDADES V23 - SISTEMA DUO (DOS ESP32-C5)
--------------------------------------------------------------------------------
* POR QUE: el evil twin en 5 GHz con UNA sola placa tiene un limite fisico:
  una radio no puede mantener el portal del AP falso Y saltar entre bandas
  haciendo deauth al mismo tiempo. Con DOS placas se resuelve:
  - PLACA 1 (con pantalla, este firmware): sirve el AP falso + portal.
  - PLACA 2 (SX-DRONE, firmware SALARINI_DRONE_C5): hace TODO el deauth
    en el canal de la victima - 2.4 GHz Y 5 GHz - sin dejar su trabajo.

* ENLACE BLUETOOTH (BLE) SOLO DURANTE EL ATAQUE (como pediste):
  - El drone se anuncia como "SX-DRONE" con direccion fija C5:D2:05:00:00:01.
  - Al lanzar un ataque DUO, la placa principal se conecta sola por BLE,
    le pasa la lista de objetivos (TCLR / T+canal:bssid / GO) y al terminar
    el ataque le manda STOP y libera el enlace.
  - En el evil twin, si el drone esta enlazado ACOMPAÑA SIEMPRE (radio extra).

* MENU DRONE DUO (ADVANCED > Drone Duo (2 boards)) con estado en vivo:
  1. LINK DRONE   - enlaza manualmente (para probar / estado).
  2. UNLINK DRONE - libera el radio BLE.
  3. DUO EVIL TWIN (5GHz) - portal aqui + drone martillando TODOS los
     BSSID de la red (2.4 y 5 GHz): EL ARREGLO DEL PROBLEMA 5 GHZ.
  4. DUO CROSSFIRE - las dos radios golpear el mismo objetivo a la vez.
  5. DUO PINCER 2.4+5G - esta placa ataca la banda 2.4, el drone la 5.0.
  6. DUO HANDSHAKE - el drone hace el deauth y esta placa se queda como
     sniffer EAPOL a tiempo completo: capturas mucho mas rapidas.
  7. DRONE STATUS - estado del drone (ST/CH/TG/TX) al momento.

* DRONE (SALARINI_DRONE_C5.ino, firmware aparte y MUY ligero):
  - Sin pantalla, sin SD, sin PSRAM: buffer estaticos, cero asignaciones
    en el camino caliente. Pensado para la placa de 4 MB sin PSRAM.
  - Mismo motor de frames que el principal: deauth 0xC0 + disassoc 0xA0
    con reasons rotativos (7/4/15) + logoff cliente->AP (anti-WIDS).
  - Hasta 8 objetivos con rotacion de canales (incluye 5 GHz).
  - JAM de canal entero: objetivo ff:ff:ff:ff:ff:ff = deauth broadcast.
  - Protocolo BLE por texto (PING/INFO/STAT/STOP/TCLR/T+/GO/CH:): TAMBIEN
    se puede controlar desde el MOVIL con una app de terminal BLE
    (escribe los comandos a mano en la caracteristica RX).
  - Ritmo GO:1/2/3 = 24/10/2 ms entre rabos, 2/4/8 frames por rabo.

* PROTECCIONES: el BLE spam no arranca con el drone enlazado (y viceversa
  al enlazar) para no romper el enlace; el exito del portal para el drone
  automaticamente ([DRONE] JAM STOPPED en la terminal).

--------------------------------------------------------------------------------
NOVEDADES V22 - ESCANEO MEJORADO
--------------------------------------------------------------------------------
* LISTA ORDENADA POR SEÑAL (lo que pediste):
  - El escaneo muestra PRIMERO los internets mas CERCANOS (señal mas
    fuerte) arriba y los menos cercanos ABAJO.
  - Cada fila pinta el dBm por cercania (VERDE cerca, AMARILLO medio,
    ROJO lejos) + una barra de señal de 4 niveles para verlo de un
    vistazo.

* EL ESCANEO SIGUE LA POTENCIA (Settings > ATTACK POWER):
  - LOW  = FAST SCAN : barrido rapido (90 ms/canal) - encuentra todas
    las redes cercanas/fuertes en pocos segundos.
  - MED  = BALANCED  : 130 ms/canal (valor por defecto del driver) -
    la MISMA eficacia completa que antes pero ~30% MAS RAPIDO.
  - HIGH = DEEP SCAN : 240 ms/canal + SEGUNDA PASADA PASIVA que solo
    escucha beacons: pilla redes ocultas, APs lejanos/silenciosos y
    canales DFS que no responden a los probes.
  - La pantalla de escaneo muestra el modo: FAST / BALANCED / DEEP y
    el indicador PASS 1/2 / PASS 2/2.

* ESCANEO DE ESTACIONES (Scan Station) tambien sigue la potencia:
  LOW rapido (380/220 ms), MED equilibrado (550/320 ms), HIGH profundo
  (800/520 ms) para cazar clientes silenciosos o con trafico rafaga.

* PACKET MONITOR: el salto de canal sigue la potencia (200/300/480 ms
  por canal) - LOW recorre rapido, HIGH escucha mas a fondo.

* Todo lo de V21 sigue activo: MEGA EVIL TWIN 3 modos, deauth 5 GHz
  corregido, plantillas de portal, terminal con creditos, gestor SD,
  IN-NET y BLE a presion maxima.

--------------------------------------------------------------------------------
NOVEDADES V21
--------------------------------------------------------------------------------
* MEGA EVIL TWIN = MENU CON 3 MODOS (ADVANCED > Mega Evil Twin):
  1. REPLICATE INTERNET : pide la contrasena REAL (teclada o capturada y
     verificada de una victima); en cuanto el dispositivo se conecta a la
     red verdadera activa NAT (NAPT) y COMPARTE EL INTERNET DE VERDAD con
     todos los clientes del AP falso. El DNS pasa de secuestrado a un
     reenviador real: la red falsa se vuelve una conexion que funciona.
  2. LIVE KEYBOARD PAGE : escribe TU PROPIO mensaje con el teclado de la
     pantalla y el portal muestra exactamente ese texto sobre el SSID
     clonado con su campo de contrasena (sin editar HTML).
  3. FIX WIFI PAGE      : el gemelo con historia de mantenimiento y
     verificacion en vivo (solo la contrasena buena "restaura" la red).

* DEAUTH EN 5 GHZ ARREGLADO (la causa de "no funciona en wifis de 5 GHz"):
  - Antes: si el softAP caia en un canal de respaldo (o el radio no
    sincronizaba la banda), los frames de deauth salian por el canal
    equivocado y no tocaban a nadie.
  - Ahora TODO el deauth sale SIEMPRE por el canal de la victima (2.4 o
    5 GHz) con resintonizacion explicita antes de cada rabo de frames.
  - Los routers de doble banda emiten el MISMO SSID en 2.4 y 5 GHz: ahora
    se atacan TODOS los BSSID de ese SSID (hasta 6), banda por banda, y
    el portal nunca se mueve de su canal.
  - Cuando una victima ya esta dentro del portal, el salto de canal se
    pausa para que la pagina vaya fluida.
  - softAP en 5 GHz: nuevo intento con banda 5G-ONLY antes de recurrir a
    los canales de respaldo.

* FLUJO DE EXITO (todos los evil twins):
  - Contrasena verificada = deauth y presion OFF; la terminal se queda en
    SUCCESSFUL con la contrasena para copiar (BACK para salir).
  - En REPLICATE INTERNET ademas: portal + AP siguen vivos CON internet
    y la terminal muestra SUCCESSFUL - INTERNET LIVE.

* MENU MEJORADO:
  - ADVANCED queda en 7 entradas (Fix WiFi vive dentro de Mega Evil Twin).
  - La lista de APs muestra la banda con color (2.4G cian / 5G amarillo)
    y LOCK/OPEN - identifica las redes 5 GHz de un vistazo.
  - Contador de items en cada menu y BACK en seleccion de AP vuelve al
    menu desde el que viniste (no a SCAN).

* PLANTILLAS DE PORTAL: ahora 7 integradas (se aniade LIVE KEYBOARD
  PAGE) + todos los *.html / *.htm de la SD. %MSG% tambien funciona en
  paginas de la SD.

* Y de V20 siguen activos: cargador Windows, terminal del portal con
  creditos en vivo + /CAPTURED.TXT, handshake/PMKID rapidos, gestor de
  archivos SD con carpetas y borrado, BLE a presion maxima, IN-NET.

--------------------------------------------------------------------------------
NOVEDADES V20
--------------------------------------------------------------------------------
* FIX WIFI TWIN (ADVANCED > Fix WiFi Twin):
  - Tercer evil twin, el mas potente: deauth inteligente de TODA el area
    (victima + todos los AP del mismo canal + barrido por canales con
    APs cada 4.5 s) y crea un AP llamado "Fix WiFi".
  - Pagina profesional con historia de "mantenimiento del proveedor":
    la victima elige SU red de una lista (con las redes escaneadas de
    verdad) y escribe su contrasena.
  - VERIFICACION REAL: cada contrasena se prueba contra la red verdadera
    con WiFi.begin. Si es correcta -> pagina de exito, se apaga el deauth
    y la terminal muestra [OK] SUCCESSFUL con la contrasena para copiar.
    Si no es correcta -> error y el portal sigue activo.

* MEGA EVIL TWIN / EVIL TWIN - FLUJO DE EXITO:
  - Cuando la contrasena capturada es la buena (se verifica de verdad),
    se apagan el deauth, el beacon, el DNS y el portal; la terminal
    queda en SUCCESSFUL - PASSWORD VERIFIED con la contrasena visible y
    BACK te devuelve al menu anterior.

* PLANTILLAS DE PORTAL (para TODOS los evil twins):
  - Antes de lanzar cualquier ataque evil se abre un selector de pagina:
    WIFI PASSWORD, GMAIL, FACEBOOK, INSTAGRAM, OFFICE 365, FIX WIFI
    STORY + todos los *.html / *.htm que haya en la raiz de la SD.
  - La pagina elegida es la que sirve el portal cautivo (con %SSID% y
    %NETS% sustituidos). /PORTAL.HTML de ejemplo se sigue escribiendo
    en la SD automaticamente.

* PANTALLA DE ATAQUE (nueva etapa V20):
  - ELIMINADAS todas las animaciones por ataque.
  - Ahora: cargador estilo Windows (bolitas girando) + el nombre de la
    accion debajo (DEAUTHING / UDP FLOODING / BLE SPAMMING ...).
  - Donde hay que mostrar informacion NO hay animacion: terminal del
    portal, estados M1-M4/PMKID del handshake, hosts del NET SCAN.

* HANDSHAKE + PMKID MAS RAPIDOS:
  - Handshake: ciclo de reconexion forzada cada 1.2 s con deauth en
    difusion + patadas dirigidas a cada cliente conocido.
  - PMKID: peticiones de asociacion al doble de ritmo (25 ms).
  - Al salir (BACK) ya NO sale el popup: vas directo al menu anterior.

* ESCANEO DE STATIONS ARREGLADO:
  - Se registran clientes aunque su AP no este en la lista de APs
    (antes se descartaban -> "algunas estaciones no aparecian").
  - Tambien se capturan por probe requests y assoc/auth requests.
  - Dwell mas largo en canales con APs conocidos y timeout de 60 s.

* BLE A PRESION MAXIMA:
  - Modos tormenta (Sour Apple / Android Crash / Apocalypse) ciclan a
    ~2 ms: cientos de popups por segundo en iOS/Android/Windows.
  - Con ATTACK POWER en MAX todos los modos van sin pausa.

* ATAQUES NUEVOS EN EL MOTOR:
  - EAPOL FLOOD (802.1X): agota los contextos de autenticacion del AP.
  - QOS NULL FLOOD: llena los buffers de reintentos del AP con clientes
    falsos dormidos.
  - RTS FLOOD: reserva el aire con tramas de control RTS (NAV de 300 us).

* GESTOR DE ARCHIVOS SD:
  - Navegacion por CARPETAS (toca un directorio para entrar, ".." para
    subir), orden directorios-primero, hasta 48 entradas.
  - Visor de texto de cualquier archivo + boton DELETE FILE para
    borrar (ideal para limpiar CAPTURED.TXT / HS_EAPOL.PCAP).

* CONFIGURACION Y MENUS:
  - Eliminadas las filas SPEAKER / AUDIO de ajustes.
  - Corregidos los menus recortados (B.T ADV mostraba 6 de 9; ADVANCED
    6 de 7; SPOOF 2 de 3: ahora se ven TODOS y el scroll funciona).

--------------------------------------------------------------------------------
QUE ES ESTO
--------------------------------------------------------------------------------
Fusion de dos proyectos para la Waveshare ESP32-C5-Touch-LCD-3.5:

  * SALARINI C5 Touch V15  -> TODO lo grafico: pantalla 320x480 ST7796,
    calavera animada, mascota de arranque, temas de color, brillo, rotacion,
    teclado tactil, microSD, altavoz ES8311, bateria AXP2101, y el apartado
    HOW TO USE (ahora explica COMO FUNCIONA cada ataque, mecanismo a mecanismo).

  * NOVA X (esp32c5)  -> el motor de ataques REAL: construccion e inyeccion
    de tramas 802.11 (deauth, auth, assoc, beacon, probe response), escaneo
    de estaciones por esnifeo promiscuo y spam BLE por NimBLE. Portado y
    muy ampliado en V16-V18 con ataques nuevos, niveles de potencia,
    Evil Twin real con portal cautivo, captura de handshakes/PMKID y la
    suite IN-NET de ataques DENTRO de la red conectada.

--------------------------------------------------------------------------------
INSTALACION EN ARDUINO IDE
--------------------------------------------------------------------------------
1. Extrae este ZIP a una carpeta nueva (borra la copia anterior primero).
2. Abre SALARINI_X_C5.ino desde la carpeta SALARINI_X_C5.
3. IDE de Arduino:
   - Placa: ESP32 Arduino > ESP32C5 Dev Module
   - ESP32 Arduino Core 3.3.x o superior
   - Flash Size: 32MB   |   PSRAM: activado
4. Instala la libreria (OBLIGATORIA desde V18 para los modulos B.T Adv):
     Library Manager -> "NimBLE-Arduino" by h2zero (version 2.x)
   El .ino incluye NimBLEDevice.h de forma INCONDICIONAL: asi el buscador
   de librerias de Arduino SIEMPRE la descubre. En versiones anteriores el
   include iba dentro de __has_include() y el buscador no la veia nunca
   (por eso salia el aviso de "instala NimBLE" teniendola instalada).
   Si al compilar sale "NimBLEDevice.h: No such file or directory",
   instala la libreria y vuelve a compilar.
5. Compila y sube por USB.

Archivos que DEBEN ir juntos al .ino:
   AttackEngine.h    - motor de ataques Wi-Fi (portado de Nova X + extras)
   LanAttacks.h      - suite IN-NET (ataques dentro de la wifi conectada)
   BLESpammer.h      - motor de spam BLE (NimBLE)
   BLEDevices.h      - payloads BLE Apple/Samsung/Android/Windows
   SmoothFont.h      - fuente (Salarini)
   SkullSprites.h    - calavera animada (Salarini)
   SalariniSprites.h - mascota de arranque (Salarini)

--------------------------------------------------------------------------------
NUEVO EN V18
--------------------------------------------------------------------------------
* ARREGLADO: error de compilacion WiFi.softAPdisconnect (nombre correcto del
  API del core, con "d" minuscula).
* ARREGLADO: el modulo BLE ya no pide NimBLE si esta instalada (include
  incondicional + pragma de diagnostico: al compilar veras
  "BLE spam ENABLED" cuando todo este bien).
* NUEVOS ATAQUES WIFI:
    - Deauth 2-Way (WIDS)  : deauth bidireccional AP<->clientes con rotacion
                             de reason codes (7/4/15) y mezcla disassoc.
    - Probe Reply Flood    : responde a cada probe request con 4 redes falsas
                             (llena las listas Wi-Fi de todos los moviles).
    - Client Hunter        : esnifea clientes de un AP y los expulsa en bucle
                             automatico (elige el AP objetivo).
  Ademas TODOS los deauth ahora rotan reason codes y mezclan disassoc, y el
  radio se pone a maxima potencia de transmision (21 dBm).
* NUEVO MENU IN-NET (Connect WiFi > IN-NET ATTACKS, requiere estar conectado):
    - Net Scan    : descubre hosts de la LAN por SSDP + mDNS en 4 segundos.
                    Toca un host para usarlo de objetivo.
    - UDP Flood   : datagramas de 1 KB a broadcast o al host elegido.
    - TCP Flood   : churn de conexiones TCP (presion de estado en el target).
    - HTTP Flood  : GET serializados contra la web del router/camara/iot.
    - DHCP Starve : agota el pool de leases con DISCOVER de MACs aleatorias.
    - SSDP Flood  : tormenta M-SEARCH (amplificacion UPnP dentro de la LAN).
    - LAN Apocalypse : todo lo anterior rotado desde un solo bucle.
* BLE AMPLIADO:
    - Sour Apple iOS   : solo payloads de modal de accion de Apple a tasa
                         maxima (congela el dialogo Bluetooth del iPhone).
    - Android Crash    : Fast Pair a tasa maxima.
    - BLE Apocalypse   : rota TODAS las familias sin pausa.
  Los modulos BLE normales ahora respetan el nivel de potencia LOW/MED/MAX.
* CPU a 240 MHz + potencia TX maxima: UI mas fluida y ataques mas rapidos.

--------------------------------------------------------------------------------
MENU PRINCIPAL
--------------------------------------------------------------------------------
  EXPLOIT         -> todos los ataques (ver abajo)
  SCAN            -> escaneo de APs (real, 2.4+5 GHz), escaneo de ESTACIONES
                     (real, por esnifeo de tramas de datos) y Packet Monitor
  CONNECT WIFI    -> cliente Wi-Fi normal + IN-NET ATTACKS (ataques dentro
                     de la red ya conectada)
  PACKET MONITOR  -> monitor pasivo por canal y tipo + contador EAPOL
  SETTINGS        -> seleccion de objetivos, brillo, rotacion, temas, SD,
                     altavoz, ATTACK POWER (LOW/MED/MAX), HOW TO USE
  ABOUT           -> informacion del dispositivo

--------------------------------------------------------------------------------
ATAQUES (EXPLOIT) - QUE ES CADA UNO Y PARA QUE SIRVE
--------------------------------------------------------------------------------
DEAUTH (desautenticacion: echa clientes de la red)
  * All APs ......... deauth en broadcast a TODOS los APs, ordenados por
                      senal: primero los mas CERCANOS.
  * By Channel ...... deauth solo a los APs del canal elegido (grid 2.4/5GHz).
  * Selected APs .... marca APs y CONFIRMA (boton verde). Solo caen esos.
  * Selected STAs ... elige CLIENTES concretos del escaneo de estaciones.
  * All STAs ........ deauth a todos los clientes registrados en el esnifeo.
  * Storm (Hop) ..... salta por todos los canales deauthando todo lo que hay.
  * 2-Way (WIDS) .... deauth bidireccional con evasion de sistemas de
                      deteccion: frames AP->clientes Y clientes->AP, con
                      reason codes rotando (7/4/15) y mezcla disassoc.

AUTH FLOOD / ASSOC FLOOD
  * Inundaciones de autenticacion/asociacion con MACs aleatorias que agotan
    las tablas del AP (All / By Channel / Selected).

BEACON (APs falsos)
  * All SSIDs Dupe, Selected Dupe, Random, By Channel, Prefix, Rickroll,
    Fake Storm. Construyen redes falsas al nivel de radio.

AP SPOOFING
  * AP Clone (Spoof)  clona SSID/BSSID en todos los canales y responde a
                      probes con deauth periodico del AP real.
  * Probe Karma Flood responde a CADA probe con redes falsas.
  * Probe Reply Flood NUEVO: 4 redes falsas por cada probe visto (tormenta).

COMBO
  * Evil Twin : AP rogue real (softAP) con el SSID de la victima + DNS
    comodin + portal cautivo que fuerza el popup de login en
    iOS/Android/Windows/macOS/Linux + deauth continuo EN EL CANAL DE LA
    VICTIMA. Pagina custom: pon /PORTAL.HTML en la SD. Credenciales a
    /CAPTURED.TXT.
  * Mega Evil Twin : submenu con 3 modos - REPLICATE INTERNET (NAT +
    internet real), LIVE KEYBOARD PAGE (tu texto) y FIX WIFI PAGE
    (verificacion en vivo). Ver NOVEDADES V21.
  * Handshake Cap : captura EAPOL M1-M4 con deauth suave de ayuda y guarda
    /HS_EAPOL.PCAP para Wireshark.
  * PMKID Grab    : asociaciones con MACs falsas hasta que el router suelta
    el PMKID en el mensaje 1 (sin clientes y sin esperar handshakes).
  * Deauth + Rickroll, Total Outage (deauth + beacons falsos + BLE).
  * Client Hunter : elige un AP; el firmware esnifea sus clientes y los
    expulsa automaticamente en bucle.

B.T ADV (spam BLE por NimBLE)
  * Apple / iOS, Samsung, Android (Fast Pair), Windows (Swift Pair),
    All (Seq), Random Mix, Sour Apple iOS (max), Android Crash (max),
    BLE Apocalypse (todo a la vez, sin pausa).
  Efecto: los moviles cercanos muestran ventanas de emparejamiento sin parar.

IN-NET (ataques DENTRO de la wifi conectada; Connect WiFi > IN-NET ATTACKS)
  * Net Scan, UDP Flood, TCP Flood, HTTP Flood, DHCP Starve, SSDP Flood,
    LAN Apocalypse. Requieren estar conectado a la red. El objetivo por
    defecto es la pasarela (gateway); usa Net Scan para elegir otros hosts.

--------------------------------------------------------------------------------
NIVELES DE POTENCIA (Settings > ATTACK POWER)
--------------------------------------------------------------------------------
  LOW : mitad de rabos de frames, doble pausa. Pruebas discretas.
  MED : ritmo de referencia de Nova X.
  MAX : doble de frames por rabo, sin pausa extra, BLE sin limites.
  Se guarda en la NVS y afecta a TODOS los ataques (wifi, BLE e IN-NET).

--------------------------------------------------------------------------------
NOVEDADES TECNICAS
--------------------------------------------------------------------------------
- Motor en AttackEngine.h: tramas IEEE 802.11 construidas igual que Nova X
  via esp_wifi_80211_tx; servidores no bloqueantes compartiendo loop().
- Redibujos parciales por regiones (lcdPushRect): la UI sigue fluida durante
  los ataques; nada de impresiones serie dentro de la callback promiscua.
- esp_wifi_set_max_tx_power(84) + WiFi.setSleep(false) + CPU a 240 MHz en
  cada arranque de ataque: maxima potencia y maxima velocidad.
- Suite IN-NET con sockets TCP/UDP normales (WiFiClient/WiFiUDP) y ventanas
  de 150 ms para que la pantalla nunca se congele.
- Sniffer unificado: una sola callback alimenta monitor, estaciones, Karma,
  handshakes/PMKID y el Client Hunter.
- Mantiene TODO el hardware de V15: AXP2101, CH32 PWM backlight, FT6336,
  microSD, ES8311/I2S, temas, teclado, calavera y mascota.

--------------------------------------------------------------------------------
CREDITOS Y LICENCIAS
--------------------------------------------------------------------------------
- SALARINI C5 Touch V15 (UI, hardware, arte): autor del proyecto original.
- NOVA X (motor de ataques, payloads BLE): warwick320 - BSD 2-Clause
  (ver LICENSE-NOVAX.txt).
- Fuente Inter: INTER_FONT_LICENSE.txt.
- NimBLE-Arduino: h2zero (se instala aparte desde Library Manager).

USALO CON RESPONSABILIDAD.
================================================================================
