#include <msp430.h>
#include "grlib.h"
#include "Crystalfontz128x128_ST7735.h"
#include "HAL_MSP430G2_Crystalfontz128x128_ST7735.h"
#include <stdio.h>
#include "uart_STDIO.h"

//-----------PROTOTIPOS FUNCIONES--------------
void inicia_perifericos(void); //inicialización de periféricos
    void Set_Clk(void); //configura el reloj
void primera_medida(void); //medida automática de distancia posterior al conectar el móvil por Bluetooth por primera vez

void uart_putc(unsigned char c); //muestra caracter por terminal
void uart_puts(const char *str); //muestra frase por terminal
void uart_putfr(const char *str); //muestra frase y salto de línea por terminal

void buzzer(void); //control del buzzer en función de la distancia_cm
void bluetooth(void); //control de transmisión de datos vía bluetooh
    void control_timers(bool sel,bool encendido); //gestiona el encendido o apagado de los timers y la selección de que señal echo recibir (sensor delantero o trasero)
void pantalla_espera(void); //pantalla de espera simple hasta que el usuario se conecte por Bluetooth o envíe alguna señal si ya está conectado
void main_screen(void); //pantalla principal
    void pinta_fondo(void); //pinta fondo de la interfaz
    void pinta_titulo(void); //pinta el título del modo principal
    void pinta_simbolo_sonido(void); //decide si tachar o no el icono de sonido
        void dibuja_altavoz(void); //dibuja icono de sonido
    void actualiza_pantalla(void); //dibuja todo lo que se debe actualizar en función de la medida realizada
        void borra_barras(char numero); //se encarga de borrar las barras sobrantes
        void dibuja_barras(char numero); //se encarga de dibujar las barras faltantes
void settings(void); //gestiona la configuración del sistema
    void control_sel(void); //actualiza los valores de las variables de configuración y de posición del indicador
    void pantalla_settings(void); //actualiza la pantalla de configuración
void guarda_flash(bool volume,bool idioma, bool unidades, bool interface); //se encarga de guardar en flash la configuración actual para mantenerla al inicio
void carga_config(void); //lee la flash para asignar sus valores a la configuración al iniciar el programa
//---------------------------------------------

//------------VARIABLES GLOBALES---------------
Graphics_Context g_sContext;
volatile unsigned int distancia_cm; //unidades métricas
volatile unsigned int distancia_plg; //unidades imperiales
//--------------------------------------------------------
char cad_dist[15]; //cadena para mostrar el número de la distancia
volatile char t=0; //contador que incrementa su valor cada 65 ms | se utiliza para gestionar la intermitencia del buzzer
volatile bool FinRx=0; //Fun de recepción de datos
volatile char comando_bluetooth='P'; //orden recibida por bluetooth
//--------------------------------------------------------
bool volume=1; //variable para activar o desactivar pitido de buzzer
bool volume_anterior=0; //variable para revisar si se ha modificado la configuración de sonido
bool unidades=1; //1:Unidades métricas | 0:Unidades imperiales
bool idioma=1; //1:English | 0:Spanish
bool interface=1; //1:Dark | 0:Light
bool cambio_display=1; //sirve para saber si se debe actualizar o no la pantalla
bool setting=0; //indica si estamos en el menú de configuración
bool up=0,down=0,okey=0; //flags para indicar si se ha recibido orden de subida, bajada o cambio de valor respectivamente
char borrado; //número de barras a borrar en función de la distancia calculada
char dibujado; //número de barras a dibujar en función de la distancia calculada
char modo=3; //Modo: P=Parado M=Marcha R=Retroceso
unsigned long COLOR=0; //color principal de interfaz
unsigned long COLOR_inv=0x00FFFFFF; //color secundario de interfaz
char opt_sel=1; //indica cuál de las 4 opciones está elegida
char centro=35; //centro del cuadrado indicador de opción elegida
//----------------------------------------------
void main(void)
{
    WDTCTL = WDTPW | WDTHOLD;   // stop watchdog timer
    inicia_perifericos();
    carga_config();
    pantalla_espera();
    LPM0; //se mantiene en la pantalla de espera hasta ser despertado por interrupción al recibir conexión bluetooh de usuario o alguna orden
    primera_medida();
    comando_bluetooth='P'; //se considera que la conexión se hace en parado
    main_screen();
	while(1){
	    LPM0; //esperar hasta recibir orden o realizar una medición
	    bluetooth(); //modos de funcionamiento mediante comandos vía bluetooth
	    if(!setting) main_screen();
	    else settings();
        buzzer(); //actualizar señal de buzzer
    }
}

//-----------------------------------

//------RUTINAS DE INTERRUPCION----------
#pragma vector = TIMER1_A1_VECTOR //interrupción en los flancos de subida o bajada del echo
__interrupt void EchoHCSR04(void){
        static char i=0; //variable auxiliar en interrupciones por flancos de la señal echo
        static int tempo[2]; //vector para guardar en qué tiempos se producen los flancos de subida y bajada del echo
        int diff; //tiempo que la señal echo permanece a 1
        tempo[i] = TA1CCR1; //copiar el valor del timer del registro TA1CCR1
        i += 1;
        TA1CCTL1 &= ~CCIFG ; //desactivar la bandera de interrupción que se ha activado para permitir nuevas interrupciones
        if (i==2) { //cuando se reciba tanto flanco de subida como de bajada se calcula el tiempo
            diff=abs(tempo[i-1]-tempo[i-2]); //duracion señal echo en microsegundos
            distancia_cm=diff/58; //Formula Datasheet: uS / 58 = centimeter
            distancia_plg=diff/148; //Formula Datasheet: uS / 148 = inch
            i=0;
            LPM0_EXIT; //despertar al tener la medida
        }
}

#pragma vector=TIMER1_A0_VECTOR //interrupción para contar cada 65ms
__interrupt void Contador_Buzzer(void)
{
    t++;
}

#pragma vector=USCIAB0RX_VECTOR
__interrupt void Recepcion_Orden_Bluetooh(void)
{
    comando_bluetooth=UCA0RXBUF; //guardar valor recibido en esta variable
    FinRx=1; //indicar que se ha producido el fin de una recepción
    LPM0_EXIT; //despertar al recibir una orden
}

//-------------FUNCIONES--------------

void Set_Clk(void){
    BCSCTL2 = SELM_0 | DIVM_0 | DIVS_0;
    if (CALBC1_8MHZ != 0xFF) {
        __delay_cycles(100000);
        DCOCTL = 0x00;
        BCSCTL1 = CALBC1_8MHZ;      /* Set DCO to 8MHz */
        DCOCTL = CALDCO_8MHZ;
    }
    BCSCTL1 |= XT2OFF | DIVA_0;
    BCSCTL3 = XT2S_0 | LFXT1S_2 | XCAP_1;
}

void inicia_perifericos(void)
{
    Set_Clk();
    //----------PINES----------
    /*Pines de E/S */
    P1IES = 0;
    P1IFG = 0;
    P2OUT = 0;
    P2DIR = 0;
    P2IES = 0;
    P2IFG = 0;

    //Configuración de la USCI-A para modo UART:
    P1SEL2 |= BIT1 | BIT2;  //P1.1 RX, P1.2: TX
    P1SEL |= BIT1 | BIT2;
    P1DIR |= BIT2;

    //P1.6 TRIGGER (de ambos modulos ultranosido) (SALIDA) --> uso timer 0 comparador 1
    P1DIR |= BIT6; //P1.6 OUT
    P1SEL |= BIT6;  //MODO PWM
    P1SEL2 &= ~BIT6;

    //P2.1 ECHO (de un modulo ultrasonido) (ENTRADA) --> uso timer 1 comparador 1
    P2DIR &=~ BIT1; // P2.1 PWM
    P2SEL &=~ BIT1; //MODO I/O
    P2SEL2 &= ~ BIT1;

    //P2.2 ECHO (del otro modulo ultrasonido) (ENTRADA) --> uso timer 1 comparador 1
    P2DIR &=~BIT2; // P2.2 IN
    P2SEL &=~BIT2; //MODO I/O
    P2SEL2 &= ~BIT2;

    //PIN 2.4 --> TIMER 1 COMPARADOR 2 (MANEJO DEL BUZZER)
    P2DIR |= BIT4; //P2.4 OUT
    P2SEL &= ~ BIT4;  //MODO I/O (salida a 0)
    P2SEL2 &= ~BIT4;
    P2OUT &= ~BIT4;

    //--------TIMERS-------------
    //TIMER PARA TRIGGER
    TA0CTL=TASSEL_2|ID_3|MC_0;      //SMCLK, DIV=8 (1 MHZ) ,Up to CCR0 (inicialmente STOP)
    TA0CCTL1=OUTMOD_7;      //OUTMOD=7 --> PWM activa a nivel alto
    TA0CCR0=65535;   //periodo=65536 (65 ms aprox)
    TA0CCR1=10;      //DC (10 us)
    //TA0CCTL0=CCIE; //habilitamos interrupción

    //TIMER PARA ECHOS
    TA1CTL = TASSEL_2|ID_3|MC_0 ; //SMCLK, DIV=8 (1 MHZ) , Continous up (incialmente STOP)
    TA1CCTL1 = CAP | CCIE | CCIS_0 | CM_3 | SCS ; //Capture/Compare mode (for getting the time values), interrupts enable, capture/compare input, capture mode as rising edge and falling edge together, synchronizing

    //TIMER PARA PWM BUZZER (TIMER 1 COMPARADOR 2)
    TA1CCTL2=OUTMOD_7;      //OUTMOD=7 --> PWM activa a nivel alto
    TA1CCTL0=CCIE; //habilitamos interrupción

    //PANTALLA BOOSTERPACK
    Crystalfontz128x128_Init();
    Crystalfontz128x128_SetOrientation(LCD_ORIENTATION_UP);
    Graphics_initContext(&g_sContext, &g_sCrystalfontz128x128);
    Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_BLACK); //fondo en negro
    Graphics_clearDisplay(&g_sContext);
    Graphics_setFont(&g_sContext, &g_sFontCm14); //fuente


    /*------ Configuración de la USCI-A para modo UART:----------*/

    UCA0CTL1 |= UCSWRST; // Reset
    UCA0CTL1 = UCSSEL_2 | UCSWRST;
    UCA0MCTL = UCBRF_0 | UCBRS_6;
    /* UCSSEL_2 : SMCLK (8MHz)*/

    UCA0BR0 = 65; //UCA0BR1 && UCA0BR0 = 00000011 01000001
    UCA0BR1 = 3; //8M/833~=9600
    UCA0CTL1 &= ~UCSWRST; /* Quita reset */

    IFG2 &= ~(UCA0RXIFG); /* Quita flag */
    IE2 |= UCA0RXIE; /* y habilita int. */

    //FLASH
    FCTL2 = FWKEY + FSSEL_2 + 24; // 24 dividir frecuencia (dar tiempo para correcta lectura memoria flash)

    __bis_SR_register(GIE);
}

void primera_medida(void){
    control_timers(0, 1); //seleccionamos sensor delantero y lo encendemos
    t=0;
    while(t<3); //espera hasta que se realice una medida
    control_timers(0, 0); //apagamos el sensor y el timer asociado a este
}

void buzzer(void){
    const unsigned int modulo[8]={65535,50000,30000,20000,10000,5000,2000,1000}; //Duty Cycle(DC)  de señal PWM para el buzzer (modfica volumen)
    if(volume && modo!=3){ //si el volumen está activo distinguir rangos de distancia para modular PWM
        //ZONAS POSIBLES
        if(distancia_cm<4){
            TA1CCR2=modulo[0];
            if(t>=1){
                t=0;
                P2SEL ^= BIT4;  //MODO PWM ó MODO I/O
            }
        }
        else if(distancia_cm<6 && distancia_cm>=4){
            TA1CCR2=modulo[1];
            if(t>=3){
                t=0;
                P2SEL ^= BIT4;  //MODO PWM ó MODO I/O
            }
        }
        else if(distancia_cm<8 && distancia_cm>=6){
            TA1CCR2=modulo[2];
            if(t>=6){
                t=0;
                P2SEL ^= BIT4;  //MODO PWM ó MODO I/O
            }
        }
        else if(distancia_cm<10 && distancia_cm>=8){
            TA1CCR2=modulo[3];
            if(t>=10){
                t=0;
                P2SEL ^= BIT4;  //MODO PWM ó MODO I/O
            }
        }
        else if(distancia_cm<15 && distancia_cm>=10){
            TA1CCR2=modulo[4];
            if(t>=20){
                t=0;
                P2SEL &= ~ BIT4; //MODO I/O
            }
            else if(t>=15 && t<20){
                P2SEL |= BIT4;  //MODO PWM
            }
            else P2SEL &= ~ BIT4; //MODO I/O
        }
        else if(distancia_cm<20 && distancia_cm>=15){
            TA1CCR2=modulo[5];
            if(t>=30){
                t=0;
                P2SEL &= ~ BIT4; //MODO I/O
            }
            else if(t>=25 && t<30){
                P2SEL |= BIT4;  //MODO PWM
            }
            else P2SEL &= ~ BIT4; //MODO I/O
        }
        else if(distancia_cm<25 && distancia_cm>=20){
            TA1CCR2=modulo[6];
            if(t>=40){
                t=0;
                P2SEL &= ~ BIT4; //MODO I/O
            }
            else if(t>=35 && t<40){
                P2SEL |= BIT4;  //MODO PWM
            }
            else P2SEL &= ~ BIT4; //MODO I/O
        }
        else if(distancia_cm<=30 && distancia_cm>=25){
            TA1CCR2=modulo[7];
            if(t>=50){
                t=0;
                P2SEL &= ~ BIT4; //MODO I/O
            }
            else if(t>=45 && t<50){
                P2SEL |= BIT4;  //MODO PWM
            }
            else P2SEL &= ~ BIT4; //MODO I/O
        }
        else{ //No buzzer --> lejos de objeto (distancia_cm>30 cm)
            P2SEL &= ~ BIT4;  //MODO I/O (salida a 0)
            P2SEL2 &= ~BIT4;
            P2OUT &= ~BIT4;
        }
    }
    else { //si el sonido no está activo
        //Desactivar PWM del buzzer
        P2SEL &= ~ BIT4;  //MODO I/O (salida a 0)
        P2SEL2 &= ~BIT4;
        P2OUT &= ~BIT4;
    }
}

void bluetooth(void){
        if(FinRx==1){ //si se ha dado el fin de una recepción de dato
            FinRx=0;
            switch(comando_bluetooth){ //distinguimos la orden recibida y actuamos en función de ello
            case 'M': //marcha
                if(!setting){
                    uart_putfr("Modo marcha iniciado");
                    control_timers(0,1); //habilita sensor delantero
                    modo=1;
                }
                break;
            case 'R': //retroceso
                if(!setting){
                    uart_putfr("Modo retroceso iniciado");
                    control_timers(1,1); //habilita sensor trasero
                    modo=2;
                }
                break;
            case 'P': //parada
                if(!setting){
                    uart_putfr("Modo parada iniciado");
                    control_timers(0,0); //deshabilita sensores
                    modo=3;
                }
                break;
            /*-------------CONTROL CON MANDO---------------*/
            case 'O': //okey
                if(setting) //aseguramos que el usuario esté parado para permitirle acceder al menú (para evitar distracciones)
                    okey=1;
                break;
            case 'U': //up
                if(setting) //aseguramos que el usuario esté parado para permitirle acceder al menú (para evitar distracciones)
                    up=1;
                break;
            case 'D': //down
                if(setting) //aseguramos que el usuario esté parado para permitirle acceder al menú (para evitar distracciones)
                    down=1;
                break;
            case 'S': //menú de opciones
                if(modo==3){ //solo permitimos al usuario usar el menú de configuración si está parado ( para evitar distracciones al volante)
                setting=!setting;
                cambio_display=1;
                }
                break;
        }
    }
}

void control_timers(bool sel,bool encendido){
    if(encendido){ //si se quiere usar un sensor (encendido=1) se encienden los timers
        TA0CTL=TASSEL_2|ID_3|MC_1;      //SMCLK, DIV=8 (1 MHZ) ,Up to CCR0
        TA1CTL = TASSEL_2|ID_3|MC_2 ; //SMCLK, DIV=8 (1 MHZ) , Continous up
        switch(sel){ //para elegir sensor trasero o delantero
        case 0: //delantero
            TA1CCTL1 = CAP | CCIE | CCIS_0 | CM_3 | SCS ;
            P2SEL |= BIT1; //MODO PWM
            P2SEL &=~BIT2; //MODO I/O
            break;
        case 1: //trasero
            TA1CCTL1 = CAP | CCIE | CCIS_1 | CM_3 | SCS ;
            P2SEL &=~ BIT1; //MODO I/O
            P2SEL |= BIT2; //MODO PWM
            break;
        }
    }
    else { //si encendido=0 se apagan los timers y se ponen los pines a E/S normal
        P2SEL &=~ BIT1; //MODO I/O
        P2SEL &=~BIT2; //MODO I/O
        TA0CTL=TASSEL_2|ID_3|MC_0;      //desactivamos timer TRIGGER
        TA1CTL = TASSEL_2|ID_3|MC_0 ; //desactivamos timer ECHO
    }
}

void pantalla_espera(void){
    Graphics_clearDisplay(&g_sContext);
    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_BLACK);
    Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_CYAN);
    if(!idioma){
        Graphics_drawStringCentered(&g_sContext, "  Esperando  ", 14, 64, 40, OPAQUE_TEXT);
        Graphics_drawStringCentered(&g_sContext, "  conexion   ", 14, 64, 55, OPAQUE_TEXT);
        Graphics_drawStringCentered(&g_sContext, "por Bluetooh ", 14, 64, 70, OPAQUE_TEXT);
    }
    else {
        Graphics_drawStringCentered(&g_sContext, "Waiting for ", 14, 64, 40, OPAQUE_TEXT);
        Graphics_drawStringCentered(&g_sContext, " connection ", 14, 64, 55, OPAQUE_TEXT);
        Graphics_drawStringCentered(&g_sContext, "via Bluetooh", 14, 64, 70, OPAQUE_TEXT);
    }

}

void main_screen(void){
    if(cambio_display){
        comando_bluetooth='P'; //si se da un cambio de pantalla se asume que el usuario está parado
        pinta_fondo();
        pinta_titulo();
    }
    pinta_simbolo_sonido();
    actualiza_pantalla();
    cambio_display=0;
}

void pinta_fondo(void){
    //---------FONDO----------------------------
    Graphics_setBackgroundColor(&g_sContext, COLOR_inv); //fondo en blanco
    Graphics_clearDisplay(&g_sContext);
    Graphics_setForegroundColor(&g_sContext, COLOR); // lo próximo se pintará negro
    Graphics_Rectangle rect={0,25,127,115};
    Graphics_fillRectangle(&g_sContext,&rect); //rectángulo en franja central
    Graphics_setForegroundColor(&g_sContext, COLOR_inv);
    char ancho=60,altura;
    for(altura=25;altura<=45;altura++){
        Graphics_drawLineH(&g_sContext, 0, ancho, altura);
        ancho-=3;
    }
    ancho=60;
    for(altura=115;altura>=95;altura--){ //dibuja los triángulos
        Graphics_drawLineH(&g_sContext, 0, ancho, altura);
        ancho-=3;
    }
    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_RED); //remarcamos los bordes de rojo
    Graphics_drawLineH(&g_sContext, 60, 127, 25);
    Graphics_drawLineH(&g_sContext, 60, 127, 24);
    Graphics_drawLineH(&g_sContext, 60, 127, 115);
    Graphics_drawLineH(&g_sContext, 60, 127, 116);
    Graphics_drawLine(&g_sContext, 60, 24, 0, 44);
    Graphics_drawLine(&g_sContext, 60, 25, 0, 45);
    Graphics_drawLine(&g_sContext, 60, 116, 0, 96);
    Graphics_drawLine(&g_sContext, 60, 115, 0, 95);
}

void pinta_titulo(void){
    //--------------TITULO---------------------------------------
    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_BLUE);
    if(idioma)
    Graphics_drawStringCentered(&g_sContext,"Parking System", 15, 64, 8, TRANSPARENT_TEXT);
    else Graphics_drawStringCentered(&g_sContext,"Sistema Parking", 16, 64, 8, TRANSPARENT_TEXT);
    Graphics_drawLineH(&g_sContext, 12, 115, 18);
}

void pinta_simbolo_sonido(void){
    //--------------SIMBOLO SONIDO-------------------------------
    if((volume!=volume_anterior) || cambio_display){
        volume_anterior=volume;
        if(!volume){
            dibuja_altavoz();
            Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_RED);
            Graphics_drawLine(&g_sContext, 5, 119, 21, 106);
        }
        else{
            dibuja_altavoz();
        }
    }
}

void dibuja_altavoz(void){
    Graphics_setForegroundColor(&g_sContext, COLOR_inv);
    Graphics_Rectangle limpia={5, 106, 21, 119};
    Graphics_fillRectangle(&g_sContext,&limpia);
    Graphics_setForegroundColor(&g_sContext, COLOR);
    Graphics_Rectangle rect1={5,110,11,116};
    Graphics_drawRectangle(&g_sContext,&rect1);
    Graphics_drawLine(&g_sContext, 11, 110, 17, 106);
    Graphics_drawLine(&g_sContext, 11, 116, 17, 119);
    Graphics_drawLineV(&g_sContext, 17, 106, 119);
    Graphics_drawLineV(&g_sContext, 19, 110, 116);
    Graphics_drawLineV(&g_sContext, 21, 108, 118);
}

void actualiza_pantalla(void){
    //--------------dinámica de pantalla--------------
    Graphics_setForegroundColor(&g_sContext, COLOR);
    switch(modo){
    case 1:
        if(!idioma)
            Graphics_drawString(&g_sContext, " Marcha  ", 10, 63, 90, OPAQUE_TEXT);
        else Graphics_drawString(&g_sContext, " Forward ", 10, 63, 90, OPAQUE_TEXT);
        break;
    case 2:
        if(!idioma)
            Graphics_drawString(&g_sContext, "Retroceso", 10, 63, 90, OPAQUE_TEXT);
        else Graphics_drawString(&g_sContext, " Backward", 10, 63, 90, OPAQUE_TEXT);
        break;
    case 3:
        if(!idioma)
            Graphics_drawString(&g_sContext, "  Parada  ", 10, 63, 90, OPAQUE_TEXT);
        else Graphics_drawString(&g_sContext, "  Stop   ", 10, 63, 90, OPAQUE_TEXT);
        break;
    }
    if(comando_bluetooth!='P' || cambio_display){
        if(distancia_cm<30)
            borrado=5-(5-distancia_cm/5); //cálculo de barras a borrar
        else borrado=6;
        borra_barras(borrado);
        dibujado=6-borrado; //cálculo de barras a dibujar
        dibuja_barras(dibujado);
        Graphics_setForegroundColor(&g_sContext, COLOR);
        if(unidades)
                sprintf(cad_dist,"  %d cm  ",distancia_cm);
        else {
            if(!idioma) sprintf(cad_dist,"  %d plg  ",distancia_plg);
            else sprintf(cad_dist,"  %d inch",distancia_plg);
        }
        Graphics_drawString(&g_sContext,cad_dist, 15, 65, 63, OPAQUE_TEXT);
        switch(dibujado){
        case 0:
            if(!idioma)
                Graphics_drawString(&g_sContext,"Muy lejos ", 11, 62, 35, OPAQUE_TEXT);
            else Graphics_drawString(&g_sContext,"   So far    ", 11, 62, 35, OPAQUE_TEXT);
            break;
        case 1:
        case 2:
            if(!idioma)
                Graphics_drawString(&g_sContext,"  Lejano  ", 11, 62, 35, OPAQUE_TEXT);
            else Graphics_drawString(&g_sContext,"   Far    ", 11, 62, 35, OPAQUE_TEXT);
            break;
        case 3:
        case 4:
            if(!idioma)
                Graphics_drawString(&g_sContext," Proximo  ", 11, 62, 35, OPAQUE_TEXT);
            else Graphics_drawString(&g_sContext,"   Near   ", 11, 62, 35, OPAQUE_TEXT);
            break;
        case 5:
        case 6:
            if(!idioma)
                Graphics_drawString(&g_sContext,"Muy cerca ", 11, 62, 35, OPAQUE_TEXT);
            else Graphics_drawString(&g_sContext,"Very close", 11, 62, 35, OPAQUE_TEXT);
            break;
        }
    }
}

void borra_barras(char numero){
    Graphics_setForegroundColor(&g_sContext, COLOR);
    char j=0,h=35;
    for(j=54;j>54-10*numero && j<200;j-=10){ //REVISAR EL >=
        Graphics_Rectangle borra_barra={j,70-h,j+4,70+h};
        Graphics_fillRectangle(&g_sContext, &borra_barra); //borra las barras
        h-=5;
    }
}

void dibuja_barras(char numero){
    char j=0,h=5,aux=1;
    for(j=4;j<4+10*numero;j+=10){
        switch(aux){
        case 1: Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_GREEN); break;
        case 2: Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_GREEN); break;
        case 3: Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_YELLOW); break;
        case 4: Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_YELLOW); break;
        case 5: Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_RED); break;
        case 6: Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_RED); break;
        }
        Graphics_Rectangle dibuja_barra={j,70-h,j+4,70+h}; //dibuja las barras
        Graphics_fillRectangle(&g_sContext, &dibuja_barra);
        h+=5;
        aux++;
    }
}

void settings(){
    control_sel();
    if(interface){
        COLOR=0;
        COLOR_inv=0x00FFFFFF;
    }
    else{
        COLOR=0x00FFFFFF;
        COLOR_inv=0;
    }
    pantalla_settings();
}

void control_sel(void){
    //-------------CONTROL POR MANDO-----------------
    if(down){
        if(opt_sel==4){
            opt_sel=1;
            centro=35;
        }
        else {
            opt_sel++;
            centro+=25;
        }
        down=0;
    }
    else if(up){
        if(opt_sel==1){
            opt_sel=4;
            centro=110;
        }
        else{
            opt_sel--;
            centro-=25;
        }
    up=0;
    }
    if(okey){
        switch(opt_sel){
        case 1:
            volume=!volume;
            break;
        case 2:
            idioma=!idioma;
            break;
        case 3:
            unidades=!unidades;
            break;
        case 4:
            interface=!interface;
            break;
        }
        guarda_flash(volume,idioma,unidades,interface);
        okey=0;
    }
}

void pantalla_settings(void){
    //-----------Pantalla principal menú------------
    Graphics_setBackgroundColor(&g_sContext, COLOR); //fondo en blanco
    Graphics_clearDisplay(&g_sContext);
    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_DARK_GREEN);
    Graphics_Rectangle rectan={8,centro-2,12,centro+2};
    Graphics_fillRectangle(&g_sContext, &rectan); //dibujar indicador de opción
    if(idioma)
    Graphics_drawStringCentered(&g_sContext,"Settings", 10, 64, 7, TRANSPARENT_TEXT);
    else Graphics_drawStringCentered(&g_sContext,"Configuración", 10, 64, 7, TRANSPARENT_TEXT);


    Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_DARK_GREEN);
    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_GREEN_YELLOW);
    if(idioma){
        Graphics_drawString(&g_sContext,"Volume:", 9, 20,30, OPAQUE_TEXT);
        Graphics_drawString(&g_sContext,"Language:", 10, 20,55, OPAQUE_TEXT);
        Graphics_drawString(&g_sContext,"Units:", 7,20, 80, OPAQUE_TEXT);
        Graphics_drawString(&g_sContext,"Interface:", 10,20, 105, OPAQUE_TEXT);
    }
    else {
        Graphics_drawString(&g_sContext,"Volumen:", 9, 20,30, OPAQUE_TEXT);
        Graphics_drawString(&g_sContext,"Lenguaje:", 10, 20,55, OPAQUE_TEXT);
        Graphics_drawString(&g_sContext,"Unidades:", 10,20, 80, OPAQUE_TEXT);
        Graphics_drawString(&g_sContext,"Interfaz:", 10,20, 105, OPAQUE_TEXT);
    }

    //----------opciones--------
    if(volume)
        Graphics_drawString(&g_sContext,"ON", 5,80, 30, OPAQUE_TEXT);
    else Graphics_drawString(&g_sContext,"OFF", 5,80, 30, OPAQUE_TEXT);

    if(idioma)
        Graphics_drawString(&g_sContext,"ENG", 5,90, 55,OPAQUE_TEXT);
    else Graphics_drawString(&g_sContext,"ESP", 5,90, 55,OPAQUE_TEXT);

    if(unidades)
        Graphics_drawString(&g_sContext,"cm", 5,85, 80,OPAQUE_TEXT);
    else {
        if(idioma)
            Graphics_drawString(&g_sContext,"inch", 5,85, 80,OPAQUE_TEXT);
        else Graphics_drawString(&g_sContext,"plg", 5,85, 80,OPAQUE_TEXT);
    }
    if(interface){
        if(idioma)
            Graphics_drawString(&g_sContext,"Dark",8,85, 105,OPAQUE_TEXT);
        else Graphics_drawString(&g_sContext,"Oscuro",10,85, 105,OPAQUE_TEXT);
    }
    else{
        if(idioma)
            Graphics_drawString(&g_sContext,"Light",8,85, 105,OPAQUE_TEXT);
        else Graphics_drawString(&g_sContext,"Claro",8,85, 105,OPAQUE_TEXT);
    }

}

void guarda_flash(bool volume,bool idioma, bool unidades, bool interface){

    char *  Puntero = (char *) 0x1000; // Apunta a la direccion
    char copia_config[4]={volume,idioma,unidades,interface};
    char n;
        //Borra bloque
        FCTL1 = FWKEY + ERASE;       // activa  Erase
        FCTL3 = FWKEY;               // Borra Lock (pone a 0)
        *Puntero = 0;             // Escribe algo para borrar el segmento

        //FCTL3 = FWKEY;               // Borra Lock (pone a 0)
        FCTL1 = FWKEY + WRT;         // Activa WRT
        for(n=0;n<4;n++) Puntero[n]=copia_config[n]; // Escribe la configuración en 0x1000+n
        FCTL1 = FWKEY;               // Borra bit WRT
        FCTL3 = FWKEY + LOCK;        //activa LOCK
}

void carga_config(void){
    //Guarda variables en RAM
    char k;
    char *  Puntero = (char *) 0x1000; // Apunta a la direccion

    for (k=0;k<4;k++)
        switch(k){
        case 0:
            volume=Puntero[k];
            break;
        case 1:
            idioma=Puntero[k];
            break;
        case 2:
            unidades=Puntero[k];
            break;
        case 3:
            interface=Puntero[k];
            break;
        }
}
void uart_putc(unsigned char c)
{
    while (!(IFG2&UCA0TXIFG)); // Espera Tx libre
    UCA0TXBUF = c;             // manda dato
}

void uart_puts(const char *str)
{
     while(*str) uart_putc(*str++); //repite mientras !=0
}

void uart_putfr(const char *str)
{
     while(*str) uart_putc(*str++);
     uart_puts("\n");     //termina con CR
}

