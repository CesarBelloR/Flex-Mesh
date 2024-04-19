/*
 * Copyright (c) 2024 EXACT Technology
 */
#ifndef MODEM_DT_BINDINGS_H_
#define MODEM_DT_BINDINGS_H_

#define	LTE_BAND_1 (1 << 0)
#define	LTE_BAND_2 (1 << 1)
#define	LTE_BAND_3 (1 << 2)
#define	LTE_BAND_4 (1 << 3)
#define	LTE_BAND_5 (1 << 4)
#define	LTE_BAND_8 (1 << 7)
#define	LTE_BAND_12 (1 << 11)
#define	LTE_BAND_13 (1 << 12)
#define	LTE_BAND_18 (1 << 17)
#define	LTE_BAND_19 (1 << 18)
#define	LTE_BAND_20 (1 << 19)
#define	LTE_BAND_25 (1 << 24)
#define	LTE_BAND_26 (1 << 25)
#define	LTE_BAND_27 (1 << 26)
#define	LTE_BAND_28 (1 << 27)
#define	LTE_BAND_31 (1 << 30)

#define LTE_BAND_66 (1 << (65 - 64))
#define	LTE_BAND_71 (1 << (70 - 64))
#define	LTE_BAND_72 (1 << (71 - 64))
#define	LTE_BAND_73 (1 << (72 - 64))
#define	LTE_BAND_85 (1 << (84 - 64))

#endif /* MODEM_DT_BINDINGS_H_ */