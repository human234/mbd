/*
 * control.c -- SVPWM 3-phase inverter drive for STM32G474RE (NUCLEO-G474RE)
 *
 * Transplant of the STM32H743ZI Zephyr SVPWM. The H743ZI version had several
 * problems that are fixed here:
 *
 *   1. DEAD TIME: the H743ZI port wrote a bogus DTG value into TIM1->BDTR.
 *      For the G474 the correct value is DTG = 170 (~1.25 us at 170 MHz),
 *      taken from the verified G474RETEST reference project.
 *
 *   2. PWM PERIOD / SCALING: ARR is set to the G474-correct value 3935 for a
 *      21.6 kHz center-aligned switching frequency (2*3935/170e6 = 46.3 us).
 *
 *   3. SVPWM MATH: the duty computation now matches the verified G474RETEST
 *      iSVPWMz0() implementation, which produces CCR values directly in the
 *      [0, 3935] range instead of the factor-mistaken time*REAL2DIS scaling in
 *      the H743ZI version.
 *
 * The H743ZI CMSIS / Zephyr-style register access is preserved: TIM1 is set
 * up by direct register writes (bypassing the Zephyr PWM driver, which does
 * not expose center-aligned mode, complementary outputs or dead time).
 *
 * Register values verified against G474RETEST/Core/Src/main.c (MX_TIM1_Init):
 *   - Prescaler 0            (170 MHz timer clock)
 *   - Center-aligned mode 1  (CMS = 01)
 *   - ARR = 3935
 *   - PWM mode 1, active-low polarity on CH1..3 and CH1N..3N
 *   - DeadTime (DTG) = 170
 */

#include <zephyr/irq.h>
#include <stm32g474xx.h>
#include <math.h>
#include <zephyr/sys/printk.h>

/* Timer / switching parameters (G474RETEST-verified) */
#define PWM_PERIOD 3935                 /* TIM1->ARR: 2*3935/170e6 = 46.3 us */
#define SW_FREQ    21600.0f             /* 21.6 kHz switching frequency      */
#define TS         4.6296296296296296e-5f  /* 1 / SW_FREQ (full period)      */

/* Center-aligned update event fires on overflow AND underflow (2x / period) */
#define T_ISR      (TS / 2.0f)          /* 23.15 us per TIM1 update ISR      */

#define PI    3.141592653589793f
#define SQRT3 1.7320508075688772f
#define SQRT3_DIV2 0.866025403784439f
#define DEG2RAD    0.01745329251994329577f
#define PI_DIV3    1.04719755119659774615f

static float vdc = 10.0f;   /* DC bus voltage (V), matches G474RETEST Vdc  */
static float v_mag = 4.0f;  /* reference voltage magnitude (V) -> max 5 V  */
static float fout = 60.0f;  /* electrical frequency (Hz)                    */

void init_tim1_pwm(void)
{
    /* Enable TIM1 clock (APB2) */
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;

    /* Reset timer */
    TIM1->CR1 = 0;

    /* Center-aligned mode 1 (CMS = 01): counter counts up then down */
    TIM1->CR1 |= TIM_CR1_CMS_0;

    /* Prescaler 0 -> timer runs at full 170 MHz kernel clock */
    TIM1->PSC = 0;

    TIM1->ARR = PWM_PERIOD;

    /* PWM mode 1 (OCxM = 110) + preload enable for CH1, CH2 (CCMR1),
     * CH3 (CCMR2).  CH4 unused. */
    TIM1->CCMR1 |= (6 << TIM_CCMR1_OC1M_Pos);
    TIM1->CCMR1 |= TIM_CCMR1_OC1PE;
    TIM1->CCMR1 |= (6 << TIM_CCMR1_OC2M_Pos);
    TIM1->CCMR1 |= TIM_CCMR1_OC2PE;
    TIM1->CCMR2 |= (6 << TIM_CCMR2_OC3M_Pos);
    TIM1->CCMR2 |= TIM_CCMR2_OC3PE;

    /* Enable CH1..3 plus complementary CH1N..3N */
    TIM1->CCER |= TIM_CCER_CC1E  |
                  TIM_CCER_CC1NE |
                  TIM_CCER_CC2E  |
                  TIM_CCER_CC2NE |
                  TIM_CCER_CC3E  |
                  TIM_CCER_CC3NE;

    /* Active-low polarity on every output (matches G474RETEST OCPolarity and
     * OCNPolarity = LOW).  The gate driver inverts the logic level, so a
     * logical "1" (duty on) appears as low on the pin. */
    TIM1->CCER |= TIM_CCER_CC1P;
    TIM1->CCER |= TIM_CCER_CC1NP;
    TIM1->CCER |= TIM_CCER_CC2P;
    TIM1->CCER |= TIM_CCER_CC2NP;
    TIM1->CCER |= TIM_CCER_CC3P;
    TIM1->CCER |= TIM_CCER_CC3NP;

    /* Dead time DTG = 170 -> ~1.25 us at 170 MHz (correct on G474). The H743ZI
     * port carried over a bogus DTG value; 170 is the G474RETEST value. */
    TIM1->BDTR &= ~TIM_BDTR_DTG;
    TIM1->BDTR |= (170 << TIM_BDTR_DTG_Pos);

    /* Main output enable */
    TIM1->BDTR |= TIM_BDTR_MOE;

    /* Auto-reload preload (buffered ARR) */
    TIM1->CR1 |= TIM_CR1_ARPE;

    /* Initial 50% duty on all three phases */
    TIM1->CCR1 = PWM_PERIOD / 2;
    TIM1->CCR2 = PWM_PERIOD / 2;
    TIM1->CCR3 = PWM_PERIOD / 2;

    /* Generate update event to load preload registers */
    TIM1->EGR |= TIM_EGR_UG;

    /* Enable update interrupt (drives the SVPWM reference update) */
    TIM1->DIER |= TIM_DIER_UIE;

    /* Start timer */
    TIM1->CR1 |= TIM_CR1_CEN;
}

/*
 * SVPWM (7-segment, center-aligned, zero vector Z0) -- iSVPWMz0 math from
 * the verified G474RETEST project.
 *
 *  v_mag_ref : reference voltage magnitude (|V|)
 *  vdc       : DC bus voltage
 *  ph_deg    : reference phase in degrees [0, 360)
 *  ccr_buf   : out, [Ta, Tb, Tc] compare values for TIM1 CCR1..3
 */
static void svpwm(float v_mag_ref, float vdc, float ph_deg, uint32_t ccr_buf[])
{
    float theta, Rtheta, mi, miK, SINx, SINXm;
    float T1, T2, Tw;
    int sect;

    /* Modulation index: mi = 2|V| / Vdc */
    if (vdc == 0.0f) {
        mi = 0.0f;
    } else {
        mi = 2.0f * v_mag_ref / vdc;
    }

    /* miK maps into timer counts using ARR as the peak compare value */
    miK = SQRT3_DIV2 * PWM_PERIOD * mi;

    /* Sector 0..5 (60 degrees each) and angle within the sector */
    sect = (int)(ph_deg / 60.0f);
    if (sect > 5) {
        sect = 5;
    }
    theta = ph_deg - (float)sect * 60.0f;
    Rtheta = theta * DEG2RAD;

    SINXm = sinf(PI_DIV3 - Rtheta);
    SINx  = sinf(Rtheta);

    /* Active vector times (in timer counts) */
    T1 = miK * SINXm;
    T2 = miK * SINx;
    Tw = T1 + T2;

    /* Overmodulation / count clamp so CCR never exceeds ARR */
    if (Tw > (float)PWM_PERIOD) {
        float s = (float)PWM_PERIOD / Tw;
        T1 *= s;
        T2 *= s;
        Tw = (float)PWM_PERIOD;
    }

    switch (sect) {
    case 0: ccr_buf[0] = (uint32_t)Tw; ccr_buf[1] = (uint32_t)T2; ccr_buf[2] = 0; break;
    case 1: ccr_buf[0] = (uint32_t)T1; ccr_buf[1] = (uint32_t)Tw; ccr_buf[2] = 0; break;
    case 2: ccr_buf[0] = 0;            ccr_buf[1] = (uint32_t)Tw; ccr_buf[2] = (uint32_t)T2; break;
    case 3: ccr_buf[0] = 0;            ccr_buf[1] = (uint32_t)T1; ccr_buf[2] = (uint32_t)Tw; break;
    case 4: ccr_buf[0] = (uint32_t)T2; ccr_buf[1] = 0;            ccr_buf[2] = (uint32_t)Tw; break;
    default: ccr_buf[0] = (uint32_t)Tw; ccr_buf[1] = 0;           ccr_buf[2] = (uint32_t)T1; break;
    }
}

static inline void set_three_phase_pwm(uint32_t a, uint32_t b, uint32_t c)
{
    TIM1->CCR1 = a;
    TIM1->CCR2 = b;
    TIM1->CCR3 = c;
}

static void timer_update_isr(void)
{
    TIM1->SR &= ~TIM_SR_UIF;

    static float theta = 0.0f;

    /* Advance the electrical angle each update (43.2 kHz here) */
    theta += 2.0f * PI * fout * T_ISR;
    if (theta >= 2.0f * PI) {
        theta -= 2.0f * PI;
    }

    /* Generate a rotating reference voltage vector (open-loop test). */
    uint32_t ccr_buf[3];
    svpwm(v_mag, vdc, theta * (180.0f / PI), ccr_buf);

    set_three_phase_pwm(ccr_buf[0], ccr_buf[1], ccr_buf[2]);
}

void init_tim1_irq(void)
{
    IRQ_CONNECT(TIM1_UP_TIM16_IRQn, 0, timer_update_isr, NULL, 0);
    irq_enable(TIM1_UP_TIM16_IRQn);
}

void set_frequency(float fre)
{
    fout = fre;
}

void set_magnitude(float mag)
{
    v_mag = mag;
}

/* Stop the SVPWM: halt the timer, force every output to a safe (off)
 * compare value and disable the main outputs so the bridge is truly dead
 * rather than parked on the last running vector. */
void svpwm_stop(void)
{
    TIM1->CR1 &= ~TIM_CR1_CEN;

    /* 0 = full active-low -> gate driver off: safe bridge. */
    set_three_phase_pwm(0, 0, 0);

    /* With CCxPE preload enabled and the counter stopped, the CCR writes
     * above only land in the preload registers. Force an update event so the
     * shadow compares are reloaded too. */
    TIM1->EGR |= TIM_EGR_UG;

    /* Disable the six PWM outputs (MOE=0): zero volts to the motor, not just
     * a stopped switching pattern. */
    TIM1->BDTR &= ~TIM_BDTR_MOE;
}

/* (Re)start the SVPWM: re-enable the main outputs and the timer so the
 * rotating vector ISR resumes. */
void svpwm_start(void)
{
    TIM1->BDTR |= TIM_BDTR_MOE;
    TIM1->CR1 |= TIM_CR1_CEN;
}
