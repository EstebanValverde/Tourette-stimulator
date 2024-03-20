// ATMEL ATTINY 25/45/85 / ARDUINO
//
//                  +-\/-+
// Ain0 (D 5) PB5  1|    |8  Vcc
// Ain3 (D 3) PB3  2|    |7  PB2 (D 2) Ain1, INT0
// Ain2 (D 4) PB4  3|    |6  PB1 (D 1) pwm1
//            GND  4|    |5  PB0 (D 0) pwm0
//                  +----+

#include <avr/sleep.h>    // Sleep Modes
#include <avr/power.h>    // Power management

#define MAXTRIALS 190 // 190 trials equivale a 15 minutos de estimulacion

const byte PULSES = 0;    // pin 5 (PB0), salida de trenes de pulsos
const byte HIGHPOWER = 1; // pin 6 (PB1), enable tension de 28V
const byte PININT0 = 2;   // pin 7 (PB2), entrada de interrupcion INT0 / digital input PB2
const byte PINLED = 4;    // pin 3 (PB4), Led para indicar que el protocolo esta activo

// ---------------------------------------------------------
void setup(void)
{
	pinMode(HIGHPOWER, OUTPUT); // control Enable fuente alta tension
	digitalWrite(HIGHPOWER, LOW);  // apago la fuente de 28V
	
	pinMode(PULSES, OUTPUT); // salida del tren de pulsos
	digitalWrite(PULSES, LOW);  // sin pulsos de salida

	pinMode(PINLED, OUTPUT); // salida del tren de pulsos
	digitalWrite(PINLED, LOW);  // sin pulsos de salida

	// mando a dormir y espero INT0
	goToSleep();
}

// ---------------------------------------------------------
void loop(void)
{
  // por ahora usa un flag para hacer un break dentro de ciclos
  // for anidados
  bool abort = false;
  unsigned long inicio;

  // durante MAXTRIALS envio el tren de pulsos y 4 segundos
  // de espera, luego de ese tiempo, deja de estimular
  for(unsigned char trials = 0; (trials < MAXTRIALS) & !abort; trials++)
  {
    // envio el tren de pulsos
    protocoloPulsos();
    
    // chequeo PB2, si esta en LOW es porque cancelo el protocolo.
    // Lo hago durante 4 segundos porque durante ese tiempo voy a estar
    // mirando el pin de abort (pin 7 del micro, mismo que INT0)
    inicio = millis();
    while((millis()-inicio < 4000) & !abort)
    {
      if(digitalRead(PININT0) == LOW) abort = true;
    }
  }
  // espero a que se deje de presionar el pulsador, porque puede sucder que el
  // paciente mantenga apretado el pulsador por más tiempo
  while(digitalRead(PININT0) == LOW);

  // si llegue aqui es porque o bien termino el protocolo de 15 minutos
  // de pulsos o porque aborte. Espero 4 segundos adicionales antes de
  // dormir al micro y esperar a un nuevo protocolo. Esto lo hago para
  // evitar que algun rebote del boton de abortar, que es el mismo que
  // despues se configura como INT0 para desperar el micro, precisamente
  // quede disparando una interrupcion y ni bien se aborta arranca un
  // nuevo protocolo
  delay(4000);
    
  // duermo al micro hasta que lo despierto con INT0
  goToSleep();
}
  
// ---------------------------------------------------------
// hardware interrupt INT0
ISR(INT0_vect)
{
  // despierta al micro y inicializa nuevamente el loop
}
 
// ---------------------------------------------------------
void INT0_enable(void)
{
  // al configurar INT0 en el pin 7, automaticamente deja
  // de comportarse como pin de entrada/salida digital
  // digitalWrite(PININT0, LOW);  
  GIFR  &= ~bit(INTF0);  // clear any outstanding interrupts
	GIMSK |= bit(INT0);    // enable pin change interrupts
}  

// ---------------------------------------------------------
void INT0_disable(void)
{
  GIFR  &= ~bit(INTF0);  // clear any outstanding interrupts
	GIMSK &= ~bit(INT0);   // unable pin change interrupts
  // como inhabilito INT0 por el pini 7, ahora puedo reconfigurarlo
  // como pin de entrada salida, para leer digitalmente este pin.
  // esto lo uso para que el usuario tenga la opcion de abortar durante
  // el protocolo. No logre hacer que INT0 funcione para despertar/abortar
  // siempre se quedaba reentrando al interrupcion y no pdia salir de ahi
	pinMode(PININT0, INPUT); // pongo el mismo pin de INT0 ahora como digital input
	digitalWrite(PININT0, HIGH); // internal pull-up
}  

// ---------------------------------------------------------
void goToSleep(void)
{
	// en power-down, quedan despiertos INT0 y WTC
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
	ADCSRA = 0;           // turn off ADC before power-down ADC <-- importante
	power_all_disable();  // power everything off. Apago todos los modulos del micro
	
	// Seguramente uso el par [noInterrupts, interrups] para evitar que mientras preparo
	// todo para pasar a sleep mode, no ingrese ninguna interrupcion
	noInterrupts();       // timed sequence coming up. 
	sleep_enable();       // Ya está el micro prearado para dormirse
	interrupts();         // Re-enables interrupts. interrupts are required now
  
  // --------------------------------------------------------------------------
  // activo interrupcion INT0 en pin 7 y desactivo entrada digital
  INT0_enable();

	// aca se pone a dormir el Arduino... y se queda en esta linea de ejecucion hasta
  // que pase algo... en este caso una interrupcion del INT0
	sleep_cpu();          // ---->Put the device into sleep mode. 

  // inactivo interrupcion INT0 y activo entrada digital en pin 7
  INT0_disable();
  // --------------------------------------------------------------------------

  // si llego acá es porque el micro, se despertó por INT0
	sleep_disable();      // Clear the sleep mode bit.
	power_all_enable();   // power everything back on. Enciendo todos los modulos del micro

  // estas instrucciones ayudan a reducir el consumo porque son modulos
  // que no se usan en este proyecto, asi que los mantengo siempre apagados
	ADCSRA = 0;             // turn off ADC before power-down ADC <-- importante
  power_adc_disable();    // apago el ADC
  power_usi_disable();    // apago todos los puertos serie
  power_timer1_disable(); // apago el timer1. delay() y millis() usan el timer0
}
  
// ---------------------------------------------------------
void protocoloPulsos(void)
{
	// enciendo la fuente de 28V
  // espero 5 mseg para esperar el transitorio de encendido en el osciloscopio
  // se observa que el encendido dura 1mseg a 2.7V y 0.5mseg a 5V.
	digitalWrite(HIGHPOWER, HIGH);
  digitalWrite(PINLED, HIGH); // activo led para indicar trenes de pulsos
  delay(5); // milisegundos
	
	// el protocolo consta de 10 pulsos de 200useg, con una frecuencia de repeticion de 12Hz
  // (83.33mseg). En total consume 749mseg (porque luego del 10mo pulso no hay espera).
	for(unsigned char i = 0; i<9; i++)
	{  
		digitalWrite(PULSES, HIGH);
		delayMicroseconds(200);   // microsegundos
		digitalWrite(PULSES, LOW);

		// la suma total de delays (micro y mili) es 83.33 mseg que equivale a 12Hz
		delay(83); // milisegundos (99ms para 10Hz)
		delayMicroseconds(133); // microsegundos (800us para 10Hz)
	}
  // el decimo pulso...
  digitalWrite(PULSES, HIGH);
  delayMicroseconds(200);   // microsegundos
  digitalWrite(PULSES, LOW);
	
	// apago la fuente de 28V
	delay(5); // milisegundos
  digitalWrite(HIGHPOWER, LOW);
  digitalWrite(PINLED, LOW); // activo led para indicar trenes de pulsos
}
