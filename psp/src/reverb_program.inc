#include "generated/source_data/reverb_parameters.inc"
/* Generated from original E_SND_SE/079_test_fx.fpb; reverb1.FPD. */
static void source_reverb_program(SourceReverb *s,int input,int out[2])
{
    int acc=0,shift=0,memval[4]={0};unsigned cursor=s->cursor;
    (void)input;
    /* DSP step 0 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((input*16),source_reverb_coefficient[0])+(0);
    /* DSP step 1 */
    shift=source_reverb_clip(acc);acc=s->temp[(cursor+0)&127];
    int t127=shift;
    s->temp[(cursor+127)&127]=t127;
    /* DSP step 2 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t127),source_reverb_coefficient[1])+(0);
    /* DSP step 3 */
    shift=source_reverb_clip(acc);acc=0;
    int t0=shift;
    s->temp[(cursor+0)&127]=t0;
    unsigned at3=(cursor+source_reverb_delay[0])&SOURCE_REVERB_MASK;
    memval[1]=source_reverb_read(s,at3);
    /* DSP step 4 */
    shift=source_reverb_clip(acc);acc=acc;
    /* DSP step 5 */
    s->mem[0]=memval[1];
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t0),source_reverb_coefficient[2])+(acc);
    unsigned at5=(cursor+source_reverb_delay[1])&SOURCE_REVERB_MASK;
    memval[3]=source_reverb_read(s,at5);
    /* DSP step 6 */
    shift=source_reverb_clip(acc);acc=0;
    int t2=shift;
    s->temp[(cursor+2)&127]=t2;
    /* DSP step 7 */
    s->mem[1]=memval[3];
    shift=source_reverb_clip(acc);acc=acc;
    unsigned at7=(cursor+source_reverb_delay[2])&SOURCE_REVERB_MASK;
    memval[1]=source_reverb_read(s,at7);
    /* DSP step 8 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t2),source_reverb_coefficient[3])+(acc);
    /* DSP step 9 */
    s->mem[2]=memval[1];
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[0]),source_reverb_coefficient[4])+(0);
    int t4=shift;
    s->temp[(cursor+4)&127]=t4;
    unsigned at9=(cursor+source_reverb_delay[3])&SOURCE_REVERB_MASK;
    memval[3]=source_reverb_read(s,at9);
    /* DSP step 10 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[1]),source_reverb_coefficient[5])+(acc);
    /* DSP step 11 */
    s->mem[3]=memval[3];
    shift=source_reverb_clip(acc*2);acc=source_reverb_multiply((s->mem[2]),source_reverb_coefficient[6])+(0);
    int t6=shift;
    s->temp[(cursor+6)&127]=t6;
    unsigned at11=(cursor+source_reverb_delay[4])&SOURCE_REVERB_MASK;
    memval[1]=source_reverb_read(s,at11);
    /* DSP step 12 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t6),source_reverb_coefficient[7])+(acc);
    /* DSP step 13 */
    s->mem[4]=memval[1];
    shift=source_reverb_clip(acc*2);acc=source_reverb_multiply((s->temp[(cursor+9)&127]),source_reverb_coefficient[8])+(0);
    int t7=shift;
    s->temp[(cursor+7)&127]=t7;
    unsigned at13=(cursor+source_reverb_delay[5])&SOURCE_REVERB_MASK;
    memval[3]=source_reverb_read(s,at13);
    /* DSP step 14 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[3]),source_reverb_coefficient[9])+(acc);
    /* DSP step 15 */
    s->mem[5]=memval[3];
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[4]),source_reverb_coefficient[10])+(acc);
    unsigned at15=(cursor+source_reverb_delay[6])&SOURCE_REVERB_MASK;
    memval[1]=source_reverb_read(s,at15);
    /* DSP step 16 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t7),source_reverb_coefficient[11])+(acc);
    int t8=shift;
    s->temp[(cursor+8)&127]=t8;
    /* DSP step 17 */
    s->mem[6]=memval[1];
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->temp[(cursor+12)&127]),source_reverb_coefficient[12])+(0);
    int t10=shift;
    s->temp[(cursor+10)&127]=t10;
    unsigned at17=(cursor+source_reverb_delay[7])&SOURCE_REVERB_MASK;
    memval[3]=source_reverb_read(s,at17);
    /* DSP step 18 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[5]),source_reverb_coefficient[13])+(acc);
    /* DSP step 19 */
    s->mem[7]=memval[3];
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[6]),source_reverb_coefficient[14])+(acc);
    unsigned at19=(cursor+source_reverb_delay[8])&SOURCE_REVERB_MASK;
    memval[1]=source_reverb_read(s,at19);
    /* DSP step 20 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t7),source_reverb_coefficient[15])+(acc);
    int t11=shift;
    s->temp[(cursor+11)&127]=t11;
    /* DSP step 21 */
    s->mem[8]=memval[1];
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->temp[(cursor+15)&127]),source_reverb_coefficient[16])+(0);
    int t13=shift;
    s->temp[(cursor+13)&127]=t13;
    unsigned at21=(cursor+source_reverb_delay[9])&SOURCE_REVERB_MASK;
    memval[3]=source_reverb_read(s,at21);
    /* DSP step 22 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[7]),source_reverb_coefficient[17])+(acc);
    /* DSP step 23 */
    s->mem[9]=memval[3];
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[8]),source_reverb_coefficient[18])+(acc);
    unsigned at23=(cursor+source_reverb_delay[10])&SOURCE_REVERB_MASK;
    memval[1]=source_reverb_read(s,at23);
    /* DSP step 24 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t7),source_reverb_coefficient[19])+(acc);
    int t14=shift;
    s->temp[(cursor+14)&127]=t14;
    /* DSP step 25 */
    s->mem[10]=memval[1];
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->temp[(cursor+18)&127]),source_reverb_coefficient[20])+(0);
    int t16=shift;
    s->temp[(cursor+16)&127]=t16;
    unsigned at25=(cursor+source_reverb_delay[11])&SOURCE_REVERB_MASK;
    memval[3]=source_reverb_read(s,at25);
    /* DSP step 26 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[9]),source_reverb_coefficient[21])+(acc);
    /* DSP step 27 */
    s->mem[11]=memval[3];
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[10]),source_reverb_coefficient[22])+(acc);
    unsigned at27=(cursor+source_reverb_delay[12])&SOURCE_REVERB_MASK;
    memval[1]=source_reverb_read(s,at27);
    /* DSP step 28 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t7),source_reverb_coefficient[23])+(acc);
    int t17=shift;
    s->temp[(cursor+17)&127]=t17;
    /* DSP step 29 */
    s->mem[12]=memval[1];
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[11]),source_reverb_coefficient[24])+(0);
    int t19=shift;
    s->temp[(cursor+19)&127]=t19;
    unsigned at29=(cursor+source_reverb_delay[13])&SOURCE_REVERB_MASK;
    memval[3]=source_reverb_read(s,at29);
    /* DSP step 30 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[12]),source_reverb_coefficient[25])+(0);
    int t20=shift;
    s->temp[(cursor+20)&127]=t20;
    /* DSP step 31 */
    s->mem[13]=memval[3];
    shift=source_reverb_clip(acc);acc=t0;
    int t21=shift;
    s->temp[(cursor+21)&127]=t21;
    unsigned at31=(cursor+source_reverb_delay[14])&SOURCE_REVERB_MASK;
    memval[1]=source_reverb_read(s,at31);
    /* DSP step 32 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[13]),source_reverb_coefficient[26])+(t20);
    /* DSP step 33 */
    s->mem[14]=memval[1];
    shift=source_reverb_clip(acc);acc=t0;
    t20=shift;
    s->temp[(cursor+20)&127]=t20;
    unsigned at33=(cursor+source_reverb_delay[15])&SOURCE_REVERB_MASK;
    memval[3]=source_reverb_read(s,at33);
    /* DSP step 34 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[14]),source_reverb_coefficient[27])+(t21);
    /* DSP step 35 */
    s->mem[15]=memval[3];
    shift=source_reverb_clip(acc);acc=t0;
    t21=shift;
    s->temp[(cursor+21)&127]=t21;
    unsigned at35=(cursor+source_reverb_delay[16])&SOURCE_REVERB_MASK;
    memval[1]=source_reverb_read(s,at35);
    /* DSP step 36 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[15]),source_reverb_coefficient[28])+(t20);
    /* DSP step 37 */
    s->mem[16]=memval[1];
    shift=source_reverb_clip(acc);acc=t0;
    t20=shift;
    s->temp[(cursor+20)&127]=t20;
    unsigned at37=(cursor+source_reverb_delay[17])&SOURCE_REVERB_MASK;
    memval[3]=source_reverb_read(s,at37);
    /* DSP step 38 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[16]),source_reverb_coefficient[29])+(t21);
    /* DSP step 39 */
    s->mem[17]=memval[3];
    shift=source_reverb_clip(acc);acc=t0;
    t21=shift;
    s->temp[(cursor+21)&127]=t21;
    unsigned at39=(cursor+source_reverb_delay[18])&SOURCE_REVERB_MASK;
    memval[1]=source_reverb_read(s,at39);
    /* DSP step 40 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[17]),source_reverb_coefficient[30])+(t20);
    /* DSP step 41 */
    s->mem[18]=memval[1];
    shift=source_reverb_clip(acc*2);acc=t0;
    t20=shift;
    s->temp[(cursor+20)&127]=t20;
    unsigned at41=(cursor+source_reverb_delay[19])&SOURCE_REVERB_MASK;
    memval[3]=source_reverb_read(s,at41);
    /* DSP step 42 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[18]),source_reverb_coefficient[31])+(t21);
    /* DSP step 43 */
    s->mem[19]=memval[3];
    shift=source_reverb_clip(acc*2);acc=source_reverb_multiply((t20),source_reverb_coefficient[32])+(0);
    t21=shift;
    s->temp[(cursor+21)&127]=t21;
    unsigned at43=(cursor+source_reverb_delay[20])&SOURCE_REVERB_MASK;
    memval[1]=source_reverb_read(s,at43);
    /* DSP step 44 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[19]),source_reverb_coefficient[33])+(acc);
    /* DSP step 45 */
    s->mem[20]=memval[1];
    shift=source_reverb_clip(acc*2);acc=source_reverb_multiply((t21),source_reverb_coefficient[34])+(0);
    int t22=shift;
    s->temp[(cursor+22)&127]=t22;
    unsigned at45=(cursor+source_reverb_delay[21])&SOURCE_REVERB_MASK;
    memval[3]=source_reverb_read(s,at45);
    /* DSP step 46 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[20]),source_reverb_coefficient[35])+(acc);
    /* DSP step 47 */
    s->mem[21]=memval[3];
    shift=source_reverb_clip(acc*2);acc=t0;
    int t23=shift;
    s->temp[(cursor+23)&127]=t23;
    unsigned at47=(cursor+source_reverb_delay[22])&SOURCE_REVERB_MASK;
    memval[1]=source_reverb_read(s,at47);
    /* DSP step 48 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[21]),source_reverb_coefficient[36])+(0);
    /* DSP step 49 */
    s->mem[22]=memval[1];
    shift=source_reverb_clip(acc);acc=t0;
    int t24=shift;
    s->temp[(cursor+24)&127]=t24;
    /* DSP step 50 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[22]),source_reverb_coefficient[37])+(t24);
    /* DSP step 51 */
    shift=source_reverb_clip(acc*2);acc=source_reverb_multiply((t4),source_reverb_coefficient[38])+(0);
    t24=shift;
    s->temp[(cursor+24)&127]=t24;
    /* DSP step 52 */
    shift=source_reverb_clip(acc);acc=acc;
    /* DSP step 53 */
    shift=source_reverb_clip(acc);acc=t0;
    unsigned at53=(cursor+source_reverb_delay[23])&SOURCE_REVERB_MASK;
    source_reverb_write(s,at53,shift);
    /* DSP step 54 */
    shift=source_reverb_clip(acc);acc=t4;
    /* DSP step 55 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[0]),source_reverb_coefficient[39])+(0);
    unsigned at55=(cursor+source_reverb_delay[24])&SOURCE_REVERB_MASK;
    source_reverb_write(s,at55,shift);
    /* DSP step 56 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[1]),source_reverb_coefficient[40])+(acc);
    /* DSP step 57 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t6),source_reverb_coefficient[41])+(0);
    unsigned at57=(cursor+source_reverb_delay[25])&SOURCE_REVERB_MASK;
    source_reverb_write(s,at57,shift);
    /* DSP step 58 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[2]),source_reverb_coefficient[42])+(acc);
    /* DSP step 59 */
    shift=source_reverb_clip(acc);acc=t0;
    unsigned at59=(cursor+source_reverb_delay[26])&SOURCE_REVERB_MASK;
    source_reverb_write(s,at59,shift);
    /* DSP step 60 */
    shift=source_reverb_clip(acc);acc=t10;
    /* DSP step 61 */
    shift=source_reverb_clip(acc);acc=t0;
    unsigned at61=(cursor+source_reverb_delay[27])&SOURCE_REVERB_MASK;
    source_reverb_write(s,at61,shift);
    /* DSP step 62 */
    shift=source_reverb_clip(acc);acc=t13;
    /* DSP step 63 */
    shift=source_reverb_clip(acc);acc=t0;
    unsigned at63=(cursor+source_reverb_delay[28])&SOURCE_REVERB_MASK;
    source_reverb_write(s,at63,shift);
    /* DSP step 64 */
    shift=source_reverb_clip(acc);acc=t16;
    /* DSP step 65 */
    shift=source_reverb_clip(acc);acc=t0;
    unsigned at65=(cursor+source_reverb_delay[29])&SOURCE_REVERB_MASK;
    source_reverb_write(s,at65,shift);
    /* DSP step 66 */
    shift=source_reverb_clip(acc);acc=t19;
    /* DSP step 67 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t20),source_reverb_coefficient[43])+(0);
    unsigned at67=(cursor+source_reverb_delay[30])&SOURCE_REVERB_MASK;
    source_reverb_write(s,at67,shift);
    /* DSP step 68 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[19]),source_reverb_coefficient[44])+(acc);
    /* DSP step 69 */
    shift=source_reverb_clip(acc*2);acc=source_reverb_multiply((t21),source_reverb_coefficient[45])+(0);
    unsigned at69=(cursor+source_reverb_delay[31])&SOURCE_REVERB_MASK;
    source_reverb_write(s,at69,shift);
    /* DSP step 70 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((s->mem[20]),source_reverb_coefficient[46])+(acc);
    /* DSP step 71 */
    shift=source_reverb_clip(acc*2);acc=source_reverb_multiply((t22),source_reverb_coefficient[47])+(0);
    unsigned at71=(cursor+source_reverb_delay[32])&SOURCE_REVERB_MASK;
    source_reverb_write(s,at71,shift);
    /* DSP step 72 */
    shift=source_reverb_clip(acc*2);acc=source_reverb_multiply((t23),source_reverb_coefficient[48])+(0);
    int t25=shift;
    s->temp[(cursor+25)&127]=t25;
    /* DSP step 73 */
    shift=source_reverb_clip(acc*2);acc=source_reverb_multiply((t25),source_reverb_coefficient[49])+(0);
    int t26=shift;
    s->temp[(cursor+26)&127]=t26;
    /* DSP step 74 */
    shift=source_reverb_clip(acc);acc=acc;
    /* DSP step 75 */
    shift=source_reverb_clip(acc);acc=acc;
    /* DSP step 76 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t26),source_reverb_coefficient[50])+(0);
    int t126=shift;
    s->temp[(cursor+126)&127]=t126;
    /* DSP step 77 */
    shift=source_reverb_clip(acc);acc=acc;
    /* DSP step 78 */
    shift=source_reverb_clip(acc);acc=acc;
    /* DSP step 79 */
    shift=source_reverb_clip(acc);acc=t0;
    int t125=shift;
    s->temp[(cursor+125)&127]=t125;
    /* DSP step 80 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t126),source_reverb_coefficient[51])+(0);
    /* DSP step 81 */
    shift=source_reverb_clip(acc);acc=t0;
    out[0]=(int16_t)(shift>>8);
    /* DSP step 82 */
    shift=source_reverb_clip(acc);acc=source_reverb_multiply((t125),source_reverb_coefficient[52])+(0);
    /* DSP step 83 */
    shift=source_reverb_clip(acc);acc=t0;
    out[1]=(int16_t)(shift>>8);
    s->cursor=cursor==1 ? SOURCE_REVERB_MASK+1:cursor-1;
}
