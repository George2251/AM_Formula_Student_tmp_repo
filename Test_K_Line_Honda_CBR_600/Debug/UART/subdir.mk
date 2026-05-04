################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
/mnt/7A10A20610A1CA09/Personal_WorkSpace/STM_1.18_WorkSpace/myLib/UART/UART_programs.c 

OBJS += \
./UART/UART_programs.o 

C_DEPS += \
./UART/UART_programs.d 


# Each subdirectory must supply rules for building sources it contributes
UART/UART_programs.o: /mnt/7A10A20610A1CA09/Personal_WorkSpace/STM_1.18_WorkSpace/myLib/UART/UART_programs.c UART/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DSTM32F103xB -DUSE_FULL_LL_DRIVER -DHSE_VALUE=8000000 -DHSE_STARTUP_TIMEOUT=100 -DLSE_STARTUP_TIMEOUT=5000 -DLSE_VALUE=32768 -DHSI_VALUE=8000000 -DLSI_VALUE=40000 -DVDD_VALUE=3300 -DPREFETCH_ENABLE=1 -c -I../Core/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -I/mnt/7A10A20610A1CA09/Personal_WorkSpace/STM_1.18_WorkSpace/myLib/UART -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-UART

clean-UART:
	-$(RM) ./UART/UART_programs.cyclo ./UART/UART_programs.d ./UART/UART_programs.o ./UART/UART_programs.su

.PHONY: clean-UART

