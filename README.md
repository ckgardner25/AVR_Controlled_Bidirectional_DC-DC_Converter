# AVR Controlled Bidirectional DC-DC Converter
## Overview
• Designed the embedded gate-drive architecture using complementary PWM with dead-time insertion, integrating half-bridge gate drivers and an AVR microcontroller to safely control all four MOSFETs while preventing shoot-through.

• Designed and simulated a bidirectional synchronous buck-boost converter using PSpice, performing power stage design, MOSFET gate-drive analysis, and converter validation from schematic through functional verification.

• Engineered a 100 kHz complementary PWM control architecture with dead-time insertion for a four-MOSFET synchronous converter, validating gate-drive timing and switching behavior using circuit simulation.

## Block Diagram


## Control Strategy
The gate-drivers are connected to a 10V power source seperately. The signal that will be amplified/compressed goes first through the high-drive MOSFET driven by a PWM signal into its gate. Then the signal branches into a second MOSFET that has its gate tied to ground, the other half of the signal goes into a 122uH inductor to stablize the voltage. Finally, the signal will branch again into two MOSFETs both driven by a PWM signal.

<img width="420" height="450" alt="schematic" src="Simulations/Schematic-5V_to_12V.png" />

## PCB Design
<br><p float="left">
<img width="420" height="450" alt="rendering" src="Renderings/Overview.png" />
<img width="400" height="350" alt="rendering" src="Renderings/Bottom.png" />
</p>

## Simulation Results
Stabilizes in >1.0mS creating a stable 12V boosted output. This has a high efficiency of up to 96%.

<br>
<img width="420" height="450" alt="rendering" src="Simulations/3.3V-0.8A_Overview.png" />


## Measured Results


## Future Improvements

