/* TEST CODE BE CAREFUL
  AVR16EB14 bidirectional 4-switch converter controller
  Arduino core: DxCore
  Clock setting: 20 MHz
  PWM frequency: 100 kHz

  Pin mapping
  -----------
  PD4 : 3.3-5 V port voltage sense
  PD5 : 12 V port voltage sense
  PC0 : Q1 gate-driver input
  PC1 : Q3 gate-driver input
  PC2 : Q4 gate-driver input
  PF7 : UPDI -- reserved for programming

*/

#include <Arduino.h>
#include <avr/io.h>

#if F_CPU != 20000000UL
#error "Select a 20 MHz CPU clock in the DxCore Tools menu."
#endif

enum ConverterMode : uint8_t {
  MODE_OFF,
  MODE_BUCK_12_TO_5,
  MODE_BOOST_3V3_TO_12,
  MODE_BOOST_5_TO_12,
  MODE_FAULT
};

//Pins

constexpr uint8_t PIN_LOW_SENSE  = PIN_PD4;
constexpr uint8_t PIN_HIGH_SENSE = PIN_PD5;

constexpr uint8_t PIN_Q1 = PIN_PC0;
constexpr uint8_t PIN_Q3 = PIN_PC1;
constexpr uint8_t PIN_Q4 = PIN_PC2;

// ADC

constexpr float ADC_VDD = 3.300f;
constexpr uint16_t ADC_MAX = 4095;

constexpr float LOW_TOP_OHM    = 39000.0f;
constexpr float LOW_BOTTOM_OHM = 68000.0f;

constexpr float HIGH_TOP_OHM    = 100000.0f;
constexpr float HIGH_BOTTOM_OHM = 33000.0f;

constexpr uint8_t ADC_AVERAGE_SAMPLES = 16;


constexpr float LOW_SOURCE_PRESENT_V  = 2.80f;
constexpr float HIGH_SOURCE_PRESENT_V = 8.00f;

// Safeties
constexpr float BOOST_OVERVOLTAGE_V = 13.0f;
constexpr float BUCK_OVERVOLTAGE_V  = 5.5f;

constexpr float BOOST_5V_SELECTION_V = 4.20f;

// PWM

constexpr uint32_t PWM_FREQUENCY_HZ = 100000UL;
constexpr uint16_t PWM_COUNTS =
    static_cast<uint16_t>(F_CPU / PWM_FREQUENCY_HZ); // 200
constexpr uint16_t PWM_PERIOD = PWM_COUNTS - 1U;     // 199

/*
  Pulse timing taken from the supplied simulations, but overlap has been
  removed. The opposite switches are given a short all-off interval.

  BOOST from approximately 3.3 V:
    Q3 HIGH: 0.00 us to 7.65 us
    dead time: 0.10 us
    Q4 HIGH: 7.75 us to end of period

  BOOST from approximately 5 V:
    Original screenshots overlap Q3 and Q4. This safer starting preset uses:
    Q3 HIGH: 0.00 us to 6.15 us
    dead time: 0.10 us
    Q4 HIGH: 6.25 us to end of period

  BUCK from 12 V:
    Q4 HIGH: 0.00 us to 8.20 us
    dead time: 0.20 us
    Q3 HIGH: 8.40 us to end of period

  Q1 is continuously HIGH in all three supplied simulation configurations.
*/

constexpr float BOOST_3V3_Q3_OFF_US = 7.65f;
constexpr float BOOST_3V3_Q4_ON_US  = 7.75f;

constexpr float BOOST_5V_Q3_OFF_US = 6.15f;
constexpr float BOOST_5V_Q4_ON_US  = 6.25f;

constexpr float BUCK_Q4_OFF_US = 8.20f;
constexpr float BUCK_Q3_ON_US  = 8.40f;

ConverterMode converterMode = MODE_OFF;
bool faultLatched = false;

// Utlitity

uint16_t microsecondsToCounts(float microseconds)
{
  float counts = microseconds * (static_cast<float>(F_CPU) / 1000000.0f);

  if (counts < 1.0f) {
    counts = 1.0f;
  }

  if (counts > static_cast<float>(PWM_PERIOD - 1U)) {
    counts = static_cast<float>(PWM_PERIOD - 1U);
  }

  return static_cast<uint16_t>(counts + 0.5f);
}

uint16_t readAverageAdc(uint8_t pin)
{
  uint32_t total = 0;

  for (uint8_t sample = 0; sample < ADC_AVERAGE_SAMPLES; ++sample) {
    total += analogRead(pin);
  }

  return static_cast<uint16_t>(total / ADC_AVERAGE_SAMPLES);
}

float dividerInputVoltage(uint16_t adcReading,
                          float topResistance,
                          float bottomResistance)
{
  const float adcPinVoltage =
      (static_cast<float>(adcReading) * ADC_VDD) /
      static_cast<float>(ADC_MAX);

  return adcPinVoltage *
         ((topResistance + bottomResistance) / bottomResistance);
}

float readLowPortVoltage()
{
  return dividerInputVoltage(
      readAverageAdc(PIN_LOW_SENSE),
      LOW_TOP_OHM,
      LOW_BOTTOM_OHM
  );
}

float readHighPortVoltage()
{
  return dividerInputVoltage(
      readAverageAdc(PIN_HIGH_SENSE),
      HIGH_TOP_OHM,
      HIGH_BOTTOM_OHM
  );
}

// Power shutdown

void stopPowerStage()
{
  // Stop TCE before changing output ownership or inversion.
  TCE0.CTRLA = 0;
  TCE0.CTRLB = 0;

  // Remove hardware inversion.
  PORTC.PIN1CTRL &= static_cast<uint8_t>(~PORT_INVEN_bm);
  PORTC.PIN2CTRL &= static_cast<uint8_t>(~PORT_INVEN_bm);

  pinMode(PIN_Q1, OUTPUT);
  pinMode(PIN_Q3, OUTPUT);
  pinMode(PIN_Q4, OUTPUT);

  digitalWrite(PIN_Q1, LOW);
  digitalWrite(PIN_Q3, LOW);
  digitalWrite(PIN_Q4, LOW);
}

void latchFault()
{
  stopPowerStage();
  converterMode = MODE_FAULT;
  faultLatched = true;
}

// PWM config

void prepareTce()
{
  stopPowerStage();

  /*
    Route TCE0 WO0-WO3 to PC0-PC3.
    Datasheet TCEROUTEA value 0x02 selects PORTC:
      WO0=PC0, WO1=PC1, WO2=PC2, WO3=PC3.
  */
  PORTMUX.TCEROUTEA = 0x02;

  TCE0.CNT = 0;
  TCE0.PER = PWM_PERIOD;

  // Use channels 1 and 2 only: PC1/Q3 and PC2/Q4.
  TCE0.CTRLB =
      TCE_CMP1EN_bm |
      TCE_CMP2EN_bm |
      TCE_WGMODE_SINGLESLOPE_gc;

  // Count upward.
  TCE0.CTRLECLR = TCE_DIR_bm;
}

void startTce()
{
  TCE0.CNT = 0;

  // No prescaling; start timer.
  TCE0.CTRLA = TCE_CLKSEL_DIV1_gc | TCE_ENABLE_bm;
}

// PWM modes

void startBoost3V3Pwm()
{
  prepareTce();

  /*
    Q3: normal single-slope PWM, HIGH from counter bottom until CMP1.
    Q4: inverted single-slope PWM, LOW until CMP2 and HIGH afterward.
  */
  PORTC.PIN1CTRL &= static_cast<uint8_t>(~PORT_INVEN_bm);
  PORTC.PIN2CTRL |= PORT_INVEN_bm;

  TCE0.CMP1 = microsecondsToCounts(BOOST_3V3_Q3_OFF_US);
  TCE0.CMP2 = microsecondsToCounts(BOOST_3V3_Q4_ON_US);

  // Q1 is continuously ON as in the supplied simulation.
  digitalWrite(PIN_Q1, HIGH);

  startTce();
  converterMode = MODE_BOOST_3V3_TO_12;
}

void startBoost5VPwm()
{
  prepareTce();

  PORTC.PIN1CTRL &= static_cast<uint8_t>(~PORT_INVEN_bm);
  PORTC.PIN2CTRL |= PORT_INVEN_bm;

  TCE0.CMP1 = microsecondsToCounts(BOOST_5V_Q3_OFF_US);
  TCE0.CMP2 = microsecondsToCounts(BOOST_5V_Q4_ON_US);

  digitalWrite(PIN_Q1, HIGH);

  startTce();
  converterMode = MODE_BOOST_5_TO_12;
}

void startBuckPwm()
{
  prepareTce();

  /*
    Q4: normal PWM, HIGH from bottom until CMP2.
    Q3: inverted PWM, LOW until CMP1 and HIGH afterward.
  */
  PORTC.PIN2CTRL &= static_cast<uint8_t>(~PORT_INVEN_bm);
  PORTC.PIN1CTRL |= PORT_INVEN_bm;

  TCE0.CMP2 = microsecondsToCounts(BUCK_Q4_OFF_US);
  TCE0.CMP1 = microsecondsToCounts(BUCK_Q3_ON_US);

  digitalWrite(PIN_Q1, HIGH);

  startTce();
  converterMode = MODE_BUCK_12_TO_5;
}

// startup modes

void selectStartupMode()
{
  const float lowPortVoltage  = readLowPortVoltage();
  const float highPortVoltage = readHighPortVoltage();

  const bool lowSourcePresent =
      lowPortVoltage >= LOW_SOURCE_PRESENT_V;

  const bool highSourcePresent =
      highPortVoltage >= HIGH_SOURCE_PRESENT_V;

  if (highSourcePresent && !lowSourcePresent) {
    startBuckPwm();
    return;
  }

  if (lowSourcePresent && !highSourcePresent) {
    if (lowPortVoltage >= BOOST_5V_SELECTION_V) {
      startBoost5VPwm();
    } else {
      startBoost3V3Pwm();
    }
    return;
  }

  /*
    If both ports are externally powered, direction cannot be determined
    safely from voltage alone. If neither is powered, remain off.
  */
  if (highSourcePresent && lowSourcePresent) {
    latchFault();
  }
}

void setup()
{
  pinMode(PIN_LOW_SENSE, INPUT);
  pinMode(PIN_HIGH_SENSE, INPUT);

  pinMode(PIN_Q1, OUTPUT);
  pinMode(PIN_Q3, OUTPUT);
  pinMode(PIN_Q4, OUTPUT);

  analogReadResolution(12);

  stopPowerStage();

  // Let supplies and ADC divider capacitors settle.
  delay(100);

  selectStartupMode();
}

void loop()
{
  if (faultLatched || converterMode == MODE_OFF) {
    stopPowerStage();
    delay(10);
    return;
  }

  const float lowPortVoltage  = readLowPortVoltage();
  const float highPortVoltage = readHighPortVoltage();

  if ((converterMode == MODE_BOOST_3V3_TO_12 ||
       converterMode == MODE_BOOST_5_TO_12) &&
      highPortVoltage >= BOOST_OVERVOLTAGE_V) {
    latchFault();
    return;
  }

  if (converterMode == MODE_BUCK_12_TO_5 &&
      lowPortVoltage >= BUCK_OVERVOLTAGE_V) {
    latchFault();
    return;
  }

  // Open-loop preset remains active until an overvoltage fault occurs.
  delay(1);
}

// ChatGPT was used to generate this prototype code.