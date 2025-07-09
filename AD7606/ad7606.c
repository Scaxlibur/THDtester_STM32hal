#include "ad7606.h"

FIFO_t g_tAD;            //定义一个数据交换缓冲区

/*采样率*/
static uint32_t     sample_freq;                     //采样率
static float32_t    Freq;                            //计算出的最大频率
static float32_t    Amplitude;                      //最大频率的幅值
static float32_t    DC_Component;                   //直流分量

/*定义使用中的数组大小和采样点数*/
#define sample_lenth 128                         //采样点数
static float32_t InPutBuffer[sample_lenth];       //定义输入数组 InPutBuffer     该数组是外部变量 
static float32_t MidBuffer[sample_lenth];         //定义中间数组
static float32_t OutPutBuffer[sample_lenth/2];    //定义输出数组 OutPutBuffer    该数组是一个静态数组
static float32_t FreqBuffer[(sample_lenth/4)-1];       //定义除了直流分量之外的频域数组

/*定义滤波中使用的中间数组*/
#define Filter_average_num 4                             //平均滤波样本数量
float32_t Mid_Filter_Freq_Buffer[Filter_average_num];    //频率平均滤波时的中间数组


/*定义fft运算中的参数*/
uint32_t fftSize = 64;                          //FFT计算点数
uint32_t doBitReverse = 1;                      //按位取反
uint8_t ifftFlag = 0;                        //FFT变换标志，0表示FFT变换，1表示IFFT变换

/*定义计算结果存放*/
float32_t maxvalue;                             //计算出频域上最大值(包括直流分量)
uint32_t Index;                                 //最大值所在数组位置(包括直流分量)

float32_t Freq_maxvalue;                        //计算出频域上的最大值(不包括直流分量)
uint32_t Freq_Index;                            //最大值所在数组位置(不包括直流分量)

float32_t Virtual_value;                        //有效值
float32_t res;
/*定义完成转换的标志*/
static int fft_complete_flag;                   //定义转换完成标志 

/**
 * @brief 初始化ad7606
 */
void ad7606_init(void)
{
    ad7606_SetOS(0);    //设置过采样模式

    ad7606_Reset();     //复位ad7606

    AD_CONVST_HIGH();
    
}

/**
 * @brief 复位ad7606
 */
void ad7606_Reset(void)
{
    /* AD7606高电平复位，最小脉冲要求50ns */

    AD_RESET_LOW();

    AD_RESET_HIGH();
    AD_RESET_HIGH();
    AD_RESET_HIGH();
    AD_RESET_HIGH();

    AD_RESET_LOW();
}

/**
 * @brief 设置过采样模式
 * @param ucMode 0表示无过采样 1表示2倍 2表示4倍 3表示8倍 4表示16倍 5表示32倍 6表示64倍
 */
void ad7606_SetOS(uint8_t _ucMode)
{
    if (_ucMode == 1)
    {
        AD_OS2_0();
        AD_OS1_0();
        AD_OS0_1();
    }
    else if (_ucMode == 2)
    {
        AD_OS2_0();
        AD_OS1_1();
        AD_OS0_0();
    }
    else if (_ucMode == 3)
    {
        AD_OS2_0();
        AD_OS1_1();
        AD_OS0_1();
    }
    else if (_ucMode == 4)
    {
        AD_OS2_1();
        AD_OS1_0();
        AD_OS0_0();
    }
    else if (_ucMode == 5)
    {
        AD_OS2_1();
        AD_OS1_0();
        AD_OS0_1();
    }
    else if (_ucMode == 6)
    {
        AD_OS2_1();
        AD_OS1_1();
        AD_OS0_0();
    }
    else
    {
        AD_OS2_0();
        AD_OS1_0();
        AD_OS0_0();
    }
}

/**
 * @brief 启动adc转换
 */
void ad7606_StartConv(void)
{
    /* 上升沿开始转换，低电平至少持续25ns  */
    AD_CONVST_LOW();
    AD_CONVST_LOW();
    AD_CONVST_LOW();    /* 连续执行2次，低电平持续时间大约为50ns */
    AD_CONVST_LOW();
    AD_CONVST_LOW();
    AD_CONVST_LOW();
    AD_CONVST_LOW();
    AD_CONVST_LOW();
    AD_CONVST_LOW();

    AD_CONVST_HIGH();
}

/**
 * @brief spi写数据
 * @param data 待写入数据
 */
void SPI_SendData(uint16_t data)
{
    uint8_t count=0;
    AD_SCK_LOW();
    for(count=0;count<16;count++)
    {
        if(data&0x8000)
            AD_MISO_LOW();
        else
            AD_MISO_HIGH();
        data<<=1;
        AD_SCK_LOW();
        AD_CSK_HIGH();    
    }
}

/**
 * @brief spi读数据
 */
uint16_t SPI_ReceiveData(void)
{
    uint8_t count=0;
    uint16_t Num=0;
    AD_CSK_HIGH();
    for(count=0;count<16;count++)
    {
        Num<<=1;
        AD_SCK_LOW();   
        if(AD_MISO_IN)Num++;
        AD_CSK_HIGH();
    }
    return(Num);
}

/**
 * @brief 读取ADC采样结果
 * @retval usData 采样结果
 */
uint16_t ad7606_ReadBytes(void)
{
    uint16_t usData = 0;

    usData = SPI_ReceiveData();

    /* Return the shifted data */
    return usData;
}

/**
 * @brief 定时调用函数，用于读取ADC转换数据
 */
void ad7606_IRQSrc(void)
{
    uint8_t i;
    uint16_t usReadValue;
        static uint32_t j;

    /* 
    读取数据
    示波器监视，CS低电平持续时间 35us
    */
    AD_CS_LOW();
    for (i = 0; i < CH_NUM; i++)
    {
        usReadValue = ad7606_ReadBytes();
        if (g_tAD.usWrite < FIFO_SIZE)
        {
            g_tAD.usBuf[g_tAD.usWrite] = usReadValue;
            ++g_tAD.usWrite;
        }
    }

    AD_CS_HIGH();
    
    //g_tAD.usWrite = 0;

    //FFT运算
    ad7606_StartConv();
        if( j < fftSize )
        {
            InPutBuffer[2*j] = ((float)((short)g_tAD.usBuf[0])/32768/2);         //数据存放在输入数组的偶数位
            InPutBuffer[2*j+1] = 0;                                              //奇数位置零
            g_tAD.usWrite = 0;
            j++;
        }
        else if( j == fftSize)
        {
            j = 0;
            if(fft_complete_flag == 0)
            {
                memcpy(MidBuffer,InPutBuffer,sizeof(InPutBuffer));  //将输入数组的值复制到中间数组              
                fft_complete_flag = 1;
            }
            else if(fft_complete_flag == 1)
            {
                fft_complete_flag = 1;
            }
            else 
                printf("error");
        }
}

/**
 * @brief 开始采集 
 */
void ad7606_StartRecord(void)
{
    //ad7606_Reset();

    ad7606_StartConv(); 

    g_tAD.usRead = 0; 
    g_tAD.usWrite = 0;

    MX_TIM4_Init();         //设置定时器4频率
    HAL_TIM_Base_Start_IT(&htim4);         //使能定时器4中断
}

/**
 * @brief 停止采集
 */
void ad7606_StopRecord(void)
{
    HAL_TIM_Base_Stop_IT(&htim4);
}

/**
 * @brief 获取单通道的AD采样平均值
 * @param channal 选择通道，channal在1-8之间
 * @param average_num 均值法的迭代次数
 * @retval int_singal_sample_val
 */
int32_t ad7606_get_signal_average_val(int8_t channal,int8_t average_num)
{
    int i;
    float val = 0;                                                             //用于累加的中间变量
    int32_t int_singal_sample_val;
    for (i=0; i<average_num;i++)
    {
        val = val + ((float)((short)g_tAD.usBuf[channal-1])/32768/2);           //累加
    }
    g_tAD.usWrite = 0;
    int_singal_sample_val = ((int32_t)10000)*(val / average_num);           //得到平均值
    return int_singal_sample_val;
}


/**
 * @brief 获得用于分析的数组,向数组InPutBuffer中填入数据
*/
void ad7606_get_fft_data()
{
	uint32_t i;
	for (i=0;i<fftSize;i++)                                                   
	{
        printf("%ld",i);
		InPutBuffer[2*i] = ((float)((short)g_tAD.usBuf[0])/32768/2);              
		InPutBuffer[2*i+1] = 0;
		g_tAD.usWrite = 0;
		HAL_Delay(1);
	}
}

/**
 * @brief 对输入数组进行FFT运算。并且输出
		1、频率 Freq
		2、幅值 Amplitude
		3、直流分量 DC_Component
		4、周期均方（有效值）Virtual_value
 */
void fft_get_maxvalue()
{
	uint32_t k;
	
	if(fft_complete_flag == 1)
	{
		arm_cfft_f32(&arm_cfft_sR_f32_len64,MidBuffer,ifftFlag,doBitReverse);      //对输入数组进行FFT变换，变换结果将存放在输入数组中
	
	    arm_cmplx_mag_f32(MidBuffer,OutPutBuffer,fftSize);                         //对经过FFT变换的数组进行取模运算，运算结果将存放在OutPutBuffer数组中
	
	    arm_max_f32(OutPutBuffer,fftSize,&maxvalue,&Index);                        //输出数组中频域最大的数值和其所在数组中的位置

		for(k=0;k<(fftSize/2-1);k++)
		{
			FreqBuffer[k] = OutPutBuffer[k+1];                                       //取输出结果的一半，并且去除直流分量
		}
		
		arm_max_f32(FreqBuffer,(fftSize/2-1),&Freq_maxvalue,&Freq_Index);          //去除直流分量后输出数组中频域最大的数值和其所在数组中的位置
		
		Freq = (Freq_Index+1)*((float)sample_freq/(float)fftSize);                 //频率 = (N-1)*Fs/FFTSize        单位Hz
		
		Amplitude = Freq_maxvalue/((float)fftSize/2)*10000;                        //频率幅度 = value/FFTSize/2*10   单位V
		
		DC_Component = OutPutBuffer[0]/fftSize*10000;                              //直流分量 = value/FFTSize
		
		Virtual_value = Amplitude/1.4142135;                                       //有效值
		
		res = ((Virtual_value-8)/43.3)/(4-((Virtual_value-8)/43.3))*2000;
		
		//printf("maxvalue = %f \r\n location = %d  \r\n",maxvalue,Index);
		
	  printf("Fmaxvalue = %f \r\n Amplitude = %f  \r\n  DC_Component = %f  \r\n  Virtual_value = %f  \r\n Res = %f  \r\n  ",Freq,Amplitude,DC_Component,Virtual_value,res);
		
		
		fft_complete_flag = 0;                                                     //标志位置0，表示转换完成
		
	} 
}

/**
 * @brief	对fft产生的值进行滤波
 */
float32_t filter_fft()
{
	uint16_t j;
	float32_t sum=0;
	float32_t result=0;
	
	for(j=0;j<Filter_average_num;j++)
	{
		fft_get_maxvalue();
		
		sum = sum+Amplitude;
	}
	
	result = sum/4;
	
	printf("Amplitude = %f",result);
	
	return result;
}