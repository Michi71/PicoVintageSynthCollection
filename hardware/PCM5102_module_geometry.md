# GY-PCM5102 module geometry (from the STEP model, not from shop listings)

Source: /Users/michael/Downloads/pcm5102-dac-audio-modul-1/PCM5102_Modul_ksu_detail.step
(kicad StepUp assembly; sub-parts J1 = PinHeader_1x06, J2 = PinHeader_1x09, J3 = PJ-327A jack, Pcb).
Placements resolved through the assembly transforms; numbers below are exact from the model.

Origin = module PCB top-left corner, X right, Y down (KiCad convention).

    PCB outline            31.80 x 17.30 mm
    9-pin row (J2)         y = 1.625, x = 7.975 + n*2.54, n = 0..8   (x 7.975 .. 28.295)
                           pin 1 = FLT at x 7.975 (nearest the 6-pin row), pin 9 = LROUT
    6-pin row (J1)         x = 1.625, y = 2.410 + n*2.54, n = 0..5   (y 2.410 .. 15.110)
                           pin 1 = SCK at y 2.410 (nearest the 9-pin row), pin 6 = VIN
    3.5 mm jack (unused)   x 21.63 .. 31.23, y 4.80 .. 28.10
                           -> protrudes 10.8 mm past the y = 17.3 edge

The two rows are NOT on a common 2.54 grid: the 6-pin row sits 0.785 mm below the 9-pin
row's centre line and 6.35 mm to its left. The sockets on the main board have to reproduce
that offset exactly, so they cannot both sit on the board grid.

Shop listings say 32 x 24 mm, todbot says 29 x 16 mm; both are wrong for this part.
