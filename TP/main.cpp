#include <string.h>
#include "stm32f3xx.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ST7735.h"
#include "adc.h"
#include "tft.h"
#include "pinAccess.h"

#define FIFO_SIZE 128

typedef struct
{
	volatile char tab[FIFO_SIZE];
	volatile int readIndex; 			// point to the next value to read
	volatile int nb; 					// number of elements in fifo
}stFifo;

stFifo fifo;

void setup();
void serialSetup();
void serialPrintChar(char c);
void serialPrintString(char *s);
void serialPrintInt(uint32_t entier);
void reverseTable(char *table, char *reversedTable, int size);
void fifoInit(stFifo *f);
int fifoWrite(stFifo *f, char c);
int fifoRead(stFifo *f , char *c);

void setup()
{
	// ADC init
	Tft.setup();
	Tft.setTextCursor(4, 1); //col,line
	Tft.print("Hello World! ");

	ADCInit();

	pinMode(GPIOB, 0, OUTPUT);

	// Input clock = 64MHz
	RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
	__asm("nop");
	// Reset peripheral
	RCC->APB1RSTR |= RCC_APB1RSTR_TIM6RST;
	RCC->APB1RSTR &= ~RCC_APB1RSTR_TIM6RST;
	__asm("nop");

	// Configuration de l'interruption basÃ©e sur le timer6
	TIM6->DIER |= TIM_DIER_UIE;
	NVIC_EnableIRQ(TIM6_DAC1_IRQn);

	// Configuration du timer6
	TIM6->PSC = 64000-1; // ticks @ 1ms
	TIM6->ARR = 100-1; 	 // dÃ©bordement de ARR @ 10 Hz
	TIM6->CR1 |= TIM_CR1_CEN;
}

void serialSetup()
{
	// Pin config
	pinMode(GPIOA, 2, OUTPUT);
	pinAlt(GPIOA, 2, 7);

	// Setclock
	// Input clock = 32MHz
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
	__asm("nop");
	// Reset peripheral
	RCC->APB1RSTR |= RCC_APB1RSTR_USART2RST;
	RCC->APB1RSTR &= ~RCC_APB1RSTR_USART2RST;
	__asm("nop");

	// Speed config
	USART2->BRR = 278;				// baudrate @ 115200 b/s
	USART2->CR1 = USART_CR1_TE;
	USART2->CR1 |= USART_CR1_UE;
	NVIC_EnableIRQ(USART2_IRQn);
	USART2->CR1 &= ~USART_CR1_TXEIE;
}

extern "C" void USART2_IRQHandler()
{
	char ch;
	digitalWrite(GPIOB, 3, 1);
	if (fifoRead(&fifo, &ch))
		USART2->TDR = ch;
	else
		USART2->CR1 &= ~USART_CR1_TXEIE;
	digitalWrite(GPIOB, 3, 0);
}

void serialPrintChar(char c)
{
	fifoWrite(&fifo, c);
}

extern "C" void TIM6_DAC_IRQHandler()
{
	char test[] = {'T', 'E', 'S', 'T', '\0'};

	digitalWrite(GPIOB, 0, 1);
	serialPrintString(test);

	TIM6->SR &= ~TIM_SR_UIF;
	digitalWrite(GPIOB, 0, 0);
}

/**
 * send a NULL terminate ASCII string on the serial line .
 */
void serialPrintString(char *s)
{
	int lengthS = strlen(s) + 1;			// getting the \0
	for (int i = 0; i < lengthS; i++)
		serialPrintChar(s[i]);
}

/** send a 32 bits integer on the serial line . */
void serialPrintInt(uint32_t entier)
{
	int quotient = 0;
	int i = 1;
	const int MAXCHIFFRES = 10;
	char chiffres[MAXCHIFFRES + 1];

    quotient = entier / 10;
    chiffres[0] = entier % 10;

    //Si le quotient est égal à 0, le calcul de l'entier est terminé
    while ((quotient != 0) || (i == MAXCHIFFRES))
    {
        chiffres[i] = quotient % 10;
        quotient /= 10;
        i++;
    }
	chiffres[i] = '\0';

	char tabToPrint[i];

	reverseTable(chiffres, tabToPrint, i);

	serialPrintString(tabToPrint);
}

void reverseTable(char *table, char *reversedTable, int size)
{
	int index = 0;

	for (int j = (size - 1 - 1); j >= 0; j--)
	{
		reversedTable[index] = table[j];
		index++;
	}
	reversedTable[index] = '\0';
}

/** init the fifo structure */
void fifoInit(stFifo *f)
{
	__disable_irq();
	memset((void *)(f->tab), 0, FIFO_SIZE);
	f->nb = 0;
	f->readIndex = 0;
	__enable_irq();
}

/** write a char in the fifo .
* @return 0 if the fifo was full and the data was droped out
* @return 1 if the value is written in the fifo
*/
int  fifoWrite(stFifo *f, char c)
{
	int return_value;

	__disable_irq();
	int writeIndex = ((f->readIndex + f->nb) % FIFO_SIZE);
	if (f->nb != FIFO_SIZE)
	{
		f->tab[writeIndex] = c;
		f->nb++;
		return_value = 1;
		USART2->CR1 |= USART_CR1_TXEIE;
	}
	else
		return_value = 0;

	__enable_irq();
	return return_value;
}

/** read a char in the fifo .
* @return 0 if the fifo was empty . c is not updated .
* @return 1 if the value is readed . c is the read character .
*/
int fifoRead(stFifo *f ,char *c)
{
	int return_value;

	__disable_irq();
	if (f->nb == 0)
	{
		return_value = 0;
	}
	else
	{
		*c = (f->tab[f->readIndex]);
		f->readIndex = ((f->readIndex + 1) % FIFO_SIZE);
		f->nb--;
		return_value = 1;
	}

	__enable_irq();
	return return_value;
}


//this is a simple application that uses the TFT display.
// => it first write a string in the top of the screen (in setup)
// => it writes the ADC value of the potentiometer in green, and
//    refreshes it each time the value differs more than 5.
int main()
{
	static int prevPot = -1;

	setup();
	serialSetup();
	pinMode(GPIOB, 0, OUTPUT);
	pinMode(GPIOB, 3, OUTPUT);

	fifoInit(&fifo);

	prevPot = ADCRead();
	while(1)
	{
		//potentiometer
		int pot = ADCRead(); //12 bits -> 4096 -> 4 digits
		//update only we value changes significantly
		if(abs(prevPot - pot) > 50)
		{
			//set cursor centered on line 4.
			Tft.setTextCursor(Tft.getTextWidth()/2-2,4);
			Tft.eraseText(4);	//remove previous value (4 digits)
			Tft.print(pot);
			prevPot = pot;
		}
	}
}