// ATtiny85 sleep mode, wake on pin change interrupt or watchdog timer
// Author: Nick Gammon
// Date: 12 October 2013

// ATMEL ATTINY 25/45/85 / ARDUINO
//
//                  +-\/-+
// Ain0 (D 5) PB5  1|    |8  Vcc
// Ain3 (D 3) PB3  2|    |7  PB2 (D 2) Ain1
// Ain2 (D 4) PB4  3|    |6  PB1 (D 1) pwm1
//            GND  4|    |5  PB0 (D 0) pwm0
//                  +----+

#include <avr/sleep.h>    // Sleep Modes
#include <avr/power.h>    // Power management
#include <avr/wdt.h>      // Watchdog timer

const byte PULSES = 0; // pin 5 (PB0), salida de trenes de pulsos
const byte HIGHPOWER = 1; // pin 6 (PB1), enable tension de 28V
const byte PININT4 = 4; // pin 3 / (PCINT4, PB4) entrada de interrupcion

#define MAXTRIALS 10 // 190 trials equivale a 15 minutos de estimulacion
volatile unsigned int trials = MAXTRIALS; // 10 pulsos durante 760mseg + 4 seg descanso

// ---------------------------------------------------------
void setup(void)
{
	pinMode(HIGHPOWER, OUTPUT);
	digitalWrite(HIGHPOWER, LOW);  // apago la fuente de 28V
	
	pinMode(PULSES, OUTPUT); // salida del tren de pulsos
	digitalWrite(PULSES, LOW);  // sin pulsos de salida

	// configuro PB4 para entrada interrupcion
	pinMode(PININT4, INPUT); 
	// internal pull-up, entonces configuro por low level o flanco descendiente
	digitalWrite(PININT4, HIGH);  
	// activo interrupcion PCINT4 en pin PB4
	PCINT4_enable();

	// mando a dormir y espero WDT o pulsador en PCINT4
	//goToSleep();
}

// ---------------------------------------------------------
void loop(void)
{
  // hago que la interrupcion se comporte como monoetable
  PCINT4_disable();

  // durante MAXTRIALS envio el tren de pulsos y 4 segundos
  // de espera, luego de ese tiempo, deja de estimular
  if(trials < MAXTRIALS)
  {
	  trials++;
    // envio el tren de pulsos
	  protocoloPulsos();
  }
  
  // habilito nuevamente INT0
  PCINT4_enable();

  // independientemende de si estoy estimulando o no
  // mando a dormir y espero WDT o pulsador en cada ciclo
  // (cada 4 segundos).
	goToSleep();
}
  
// ---------------------------------------------------------
// hardware interrupt INT0
ISR(PCINT0_vect)
{
    trials = 0;         // inicializo el protocolo
}
 
// ---------------------------------------------------------
// watchdog interrupt
ISR(WDT_vect) 
{
  wdt_disable();  // disable watchdog
}

// ---------------------------------------------------------
void PCINT4_enable(void)
{
  MCUSR &= ~bit(ISC01);
  MCUSR &= ~bit(ISC00);  // low level interrupt
	PCMSK  = bit(PCINT4);  // want pin PB4 / pin 3
  GIFR  |= bit(PCIF);    // clear any outstanding interrupts
	GIMSK |= bit(PCIE);    // enable pin change interrupts
}  

// ---------------------------------------------------------
void PCINT4_disable(void)
{
	GIMSK &= ~bit(PCIE);    // unenable pin change interrupts
  GIFR  &= ~bit(PCIF);    // clear any outstanding interrupts
	PCMSK &= ~bit(PCINT4);  // unwant pin PB4 / pin 3
}  

// ---------------------------------------------------------
void resetWatchdog(void)
{
	// clear various "reset" flags:
	// apago WDRF (Watchdog), BORF (Brown-out), EXTRF (External), PORF (Power-on)
  MCUSR &= ~(bit(WDRF) | bit(BORF) | bit(EXTRF) | bit(PORF));
 
	// WTC change enable, disable reset, clear existing interrupt
	WDTCR = bit(WDCE) | bit(WDE) | bit(WDIF);

	// set interrupt mode and an interval (WDE must be changed from 1 to 0 here)
	// por eso se acciona este registro en dos etapas...
	// set WDIE, and 4 seconds delay [WDP3..0 = 1000]
	WDTCR = bit(WDIE) | bit(WDP3); // despierta a los 4 segundos
  // WDTCR = bit(WDIE) | bit(WDP2) | bit(WDP1); // despierta a 1 segundo

	// Reset the watchdog timer. When the watchdog timer is enabled, a call to this
	// instruction is required before the timer expires, 
	// otherwise a watchdog-initiated device reset will occur.
	wdt_reset();  
}
  
// ---------------------------------------------------------
void goToSleep(void)
{
	// en power-down, quedan despiertos INT0 y WTC
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
	ADCSRA = 0;           // turn off ADC
	
	power_all_disable();  // power everything off. Apago todos los modulos del micro
	
	// Seguramente uso el par [noInterrupts, interrups] para evitar que mientras preparo
	// todo para pasar a sleep mode, no ingrese ninguna interrupcion
	noInterrupts();       // timed sequence coming up. 
  resetWatchdog();      // get watchdog ready
	sleep_enable();       // Ya está el micro prearado para dormirse
	interrupts();         // Re-enables interrupts. interrupts are required now

	// aca se pone a dormir el Arduino... y se queda en esta
	// linea de ejecucion hasta que pase algo... en este caso
	// una interrupcion del WTC o INT0 a traves del pin PB4 del micro
	sleep_cpu();          // ---->Put the device into sleep mode. 

  // si llego acá es porque el micro
  // se despertó o bien por el WDT o por INT0
	sleep_disable();      // Clear the sleep mode bit.
	power_all_enable();   // power everything back on. Enciendo todos los modulos del micro
}
  
// ---------------------------------------------------------
void protocoloPulsos(void)
{
	// enciendo la fuente de 28V
	digitalWrite(HIGHPOWER, HIGH);
  // espero 5 mseg para esperar el transitorio de encendido
  // en el osciloscopio se observa que el encendido dura 1mseg
  // a 2.7V y 0.5mseg a 5V.
  delay(5); // milisegundos
	
	// el protocolo consta de 10 pulsos de 200useg, con una
	// frecuencia de repeticion de 12Hz (83.33mseg). En total
	// consume 749mseg (porque luego del 10mo pulso no hay espera).
	for(unsigned char i = 0; i<9; i++)
	{  
		digitalWrite(PULSES, HIGH);
		delayMicroseconds(200);   // microsegundos
		digitalWrite(PULSES, LOW);

		// la suma total de delays (micro y mili)
		// es 83.33 mseg que equivale a 12Hz
		delay(83); // milisegundos
		delayMicroseconds(133); // microsegundos
	}
  // el decimo pulso...
  digitalWrite(PULSES, HIGH);
  delayMicroseconds(200);   // microsegundos
  digitalWrite(PULSES, LOW);
	
	// apago la fuente de 28V
	delay(5); // milisegundos
  digitalWrite(HIGHPOWER, LOW);
}
