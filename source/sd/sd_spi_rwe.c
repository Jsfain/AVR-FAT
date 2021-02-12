/*
 * File    : SD_SPI_RWE.H
 * Version : 0.0.0.1 
 * Author  : Joshua Fain
 * Target  : ATMega1280
 * License : MIT
 * Copyright (c) 2020-2021
 * 
 * Implementation of SD_SPI_RWE.H
 */

#include <stdint.h>
#include <avr/io.h>
#include "usart0.h"
#include "prints.h"
#include "spi.h"
#include "sd_spi_base.h"
#include "sd_spi_rwe.h"

/*
 ******************************************************************************
 *                                 FUNCTIONS   
 ******************************************************************************
 */

/*
 * For the following read, write, and erase block functions, the returned error
 * response values can be read by their corresponding print error function. For
 * example, the returned value of sd_ReadSingleBlock() can be read by passing
 * it to sd_PrintReadError(). These print functions will read the upper byte of
 * of the returned error response. If in the error response the R1_ERROR flag 
 * is set in the upper byte, then the lower byte (i.e. the R1 Response portion
 * of the error response) contains at least one flag that has been set which 
 * should then be read by passing it to sd_PrintR1() in SD_SPI_BASE. 
 */ 

/*
 * ----------------------------------------------------------------------------
 *                                                            READ SINGLE BLOCK
 * 
 * Description : Reads a single data block from the SD card into an array.     
 * 
 * Arguments   : blckAddr     address of the data block on the SD card that    
 *                            will be read into the array.
 * 
 *               blckArr      pointer to the array to be loaded with the       
 *                            contents of the data block at blckAddr. Must be  
 *                            length BLOCK_LEN.
 * 
 * Returns     : Read Block Error (upper byte) and R1 Response (lower byte).   
 * ----------------------------------------------------------------------------
 */
uint16_t sd_ReadSingleBlock(uint32_t blckAddr, uint8_t* blckArr)
{
  uint8_t r1;                                              // for R1 responses

  CS_SD_LOW;
  sd_SendCommand(READ_SINGLE_BLOCK, blckAddr);             // CMD17
  r1 = sd_GetR1();

  if (r1 != OUT_OF_IDLE)
  {
    CS_SD_HIGH;
    return (R1_ERROR | r1);
  }
  else
  {
    uint8_t timeout = 0;

    //
    // 0xFE is the Start Block Token to be sent by
    // SD Card to signal data is about to be sent.
    //
    while (sd_ReceiveByteSPI() != 0xFE)          
    {
      timeout++;
      if (timeout > TIMEOUT_LIMIT) 
      { 
        CS_SD_HIGH;
        return (START_TOKEN_TIMEOUT | r1);
      }
    }

    // Load SD card block into the array.         
    for (uint16_t pos = 0; pos < BLOCK_LEN; pos++)
      blckArr[pos] = sd_ReceiveByteSPI();

    // Get 16-bit CRC. Don't need.
    sd_ReceiveByteSPI();
    sd_ReceiveByteSPI();
    
    // clear any remaining data from the SPDR
    sd_ReceiveByteSPI();          
  }
  CS_SD_HIGH;

  return (READ_SUCCESS | r1);
}

/*
 * ----------------------------------------------------------------------------
 *                                                           PRINT SINGLE BLOCK
 * 
 * Description : Prints the contents of a single SD card data block, previously 
 *               loaded into an array by sd_ReadSingleBlock(), to the screen. 
 *               The block's contents will be printed to the screen in rows of
 *               16 bytes, columnized as (Addr)OFFSET | HEX | ASCII.
 * 
 * Arguments   : blckArr     pointer to an array holding the contents of the 
 *                           block to be printed to the screen. Must be of 
 *                           length BLOCK_LEN.
 * 
 * Returns     : void
 * ----------------------------------------------------------------------------
 */
void sd_PrintSingleBlock(uint8_t* blckArr)
{
  const uint8_t radix = 16;                                // hex
  uint16_t offset = 0;                           // block address offset
  
  print_Str("\n\n\r BLOCK OFFSET\t\t\t\t   HEX\t\t\t\t\t     ASCII\n\r");

  for (uint16_t row = 0; row < BLOCK_LEN / radix; row++)
  {
    print_Str("\n\r   ");

    //
    // Print row offset address. This section adjust spacing depending on how
    // many digits are required to print the block offset addresses.
    //
    if (offset < 0x10)
    {
      print_Str("0x00"); 
      print_Hex(offset);
    }
    else if (offset < 0x100)
    {
      print_Str("0x0"); 
      print_Hex(offset);
    }
    else if (offset < 0x1000)
    {
      print_Str("0x"); 
      print_Hex(offset);
    }
    
    // print HEX values of the block's offset row
    print_Str("\t ");
    for (offset = row * radix; offset < row * radix + radix; offset++)
    {
      // every 4 bytes print an extra space.
      if (offset % 4 == 0) 
        print_Str(" ");
      print_Str(" ");

      // if value is not two hex digits, then first print a 0. 
      if (blckArr[offset] < 0x10)
        usart_Transmit('0');

      // print value in hex.
      print_Hex(blckArr[offset]);
    }
    
    //
    // print ASCII values of the block's offset row. Printable ascii characters
    // have decimal values between 32 and 127. If a value < 32 is encountered
    // then a space (' ') is printed. If a value >= 128 is encountered then a
    // period ('.') is printed. 
    //
    print_Str("\t\t");
    for (offset = row * radix; offset < row * radix + radix; offset++)
    {
      if (blckArr[offset] < 32)         // 32  = lower limit of printable chars 
        usart_Transmit( ' ' ); 
      else if (blckArr[offset] < 128)   // 127 = upper limit of printable chars
        usart_Transmit(blckArr[offset]);
      else 
        usart_Transmit('.');
    }
  }    
  print_Str("\n\n\r");
}
 
/*
 * ----------------------------------------------------------------------------
 *                                                           WRITE SINGLE BLOCK
 * 
 * Description : Writes the values in an array to a single SD card data block.
 * 
 * Arguments   : blckAddr     address of the data block on the SD card that 
 *                            will be written to.
 *       
 *               dataArr      pointer to an array that holds the data contents
 *                            that will be written to the block at blckAddr on
 *                            the SD card. Must be of length BLOCK_LEN.
 * 
 * Returns     : Write Block Error (upper byte) and R1 Response (lower byte).
 * ----------------------------------------------------------------------------
 */
uint16_t sd_WriteSingleBlock(uint32_t blckAddr, uint8_t* dataArr)
{
  uint8_t  r1;

  CS_SD_LOW;    
  sd_SendCommand (WRITE_BLOCK, blckAddr);                  // CMD24
  if ((r1 = sd_GetR1()) != OUT_OF_IDLE)
  {
    CS_SD_HIGH;
    return (R1_ERROR | r1);
  }
  else
  {
    const uint8_t dataTknMask = 0x1F;    // for extracting data reponse token
    uint8_t  dataRespTkn;
    uint16_t timeout = 0;

    // send Start Block Token (0xFE) to initiate data transfer
    sd_SendByteSPI(0xFE); 

    // send data to write to SD card.
    for (uint16_t pos = 0; pos < BLOCK_LEN; pos++) 
      sd_SendByteSPI (dataArr[pos]);

    // Send 16-bit CRC. CRC should be off (default), so these do not matter.
    sd_SendByteSPI(0xFF);
    sd_SendByteSPI(0xFF);
    
    // wait for valid data response token
    do
    { 
      dataRespTkn = sd_ReceiveByteSPI();
      if (timeout++ > TIMEOUT_LIMIT)
      {
        CS_SD_HIGH;
        return (DATA_RESPONSE_TIMEOUT | r1);
      }
    }
    while ((dataTknMask & dataRespTkn) != 0x05 &&          // DATA_ACCEPTED
           (dataTknMask & dataRespTkn) != 0x0B &&          // CRC_ERROR
           (dataTknMask & dataRespTkn) != 0x0D);           // WRITE_ERROR
    
    // Data Accepted --> Data Response Token = 0x05 
    if ((dataRespTkn & 0x05) == 0x05)                         
    {
      timeout = 0;

      //
      // Wait for SD card to finish writing data to the block.
      // Data Out (DO) line held low while card is busy writing to block.
      //
      while (sd_ReceiveByteSPI() == 0) 
      {
        if (timeout++ > 4 * TIMEOUT_LIMIT) 
        {
          CS_SD_HIGH;
          return (CARD_BUSY_TIMEOUT | r1);
        }
      };
      CS_SD_HIGH;
      return (DATA_ACCEPTED_TOKEN_RECEIVED | r1);
    }
    // CRC Error --> Data Response Token = 0x0B 
    else if ((dataRespTkn & 0x0B) == 0x0B) 
    {
      CS_SD_HIGH;
      return (CRC_ERROR_TOKEN_RECEIVED | r1);
    }
    // Write Error --> Data Response Token = 0x0D
    else if ((dataRespTkn & 0x0D) == 0x0D)
    {
      CS_SD_HIGH;
      return (WRITE_ERROR_TOKEN_RECEIVED | r1);
    }
  }
  return (INVALID_DATA_RESPONSE | r1) ;
}

/*
 * ----------------------------------------------------------------------------
 *                                                                 ERASE BLOCKS
 * 
 * Description : Erases the blocks between (and including) the startBlckAddr 
 *               and endBlckAddr.
 * 
 * Arguments   : startBlckAddr     address of the first block to be erased.
 * 
 *               endBlckAddr       address of the last block to be erased.
 * 
 * Returns     : Erase Block Error (upper byte) and R1 Response (lower byte).
 * ----------------------------------------------------------------------------
 */
uint16_t sd_EraseBlocks(uint32_t startBlckAddr, uint32_t endBlckAddr)
{
  uint8_t r1;                                              // for R1 responses
  
  // set Start Address for erase block
  CS_SD_LOW;
  sd_SendCommand(ERASE_WR_BLK_START_ADDR, startBlckAddr); // CMD32
  r1 = sd_GetR1();
  CS_SD_HIGH;
  if (r1 != OUT_OF_IDLE) 
    return (SET_ERASE_START_ADDR_ERROR | R1_ERROR | r1);
  
  // set End Address for erase block
  CS_SD_LOW;
  sd_SendCommand(ERASE_WR_BLK_END_ADDR, endBlckAddr);    // CMD33
  r1 = sd_GetR1();
  CS_SD_HIGH;
  if (r1 != OUT_OF_IDLE) 
    return (SET_ERASE_END_ADDR_ERROR | R1_ERROR | r1);

  // erase all blocks between, and including, start and end address
  CS_SD_LOW;
  sd_SendCommand(ERASE, 0);
  r1 = sd_GetR1 ();
  if (r1 != OUT_OF_IDLE)
  {
    CS_SD_HIGH;
    return (ERASE_ERROR | R1_ERROR | r1);
  }

  // wait for card not to finish erasing blocks.
  uint16_t timeout = 0;
  while (sd_ReceiveByteSPI() == 0)
  {
    if(timeout++ > 4 * TIMEOUT_LIMIT) 
      return (ERASE_BUSY_TIMEOUT | r1);
  }
  CS_SD_HIGH;
  return ERASE_SUCCESSFUL;
}

/*
 * If either of the three print error functions show that the R1_ERROR flag was
 * set in the error response that was passed to it, then the error response
 * should then be passed to sd_PrintR1() from SD_SPI_BASE.H/C to read the 
 * R1 Error.
 */

/*
 * ----------------------------------------------------------------------------
 *                                                             PRINT READ ERROR
 * 
 * Description : Print Read Error Flag returned by a SD card read function.  
 * 
 * Arguments   : err     Read Error Response.
 * 
 * Returns     : void
 * ----------------------------------------------------------------------------
 */
void sd_PrintReadError(uint16_t err)
{
  // 0xFF00 filters out the lower byte which is the R1 response
  switch (err & 0xFF00)
  {
    case R1_ERROR:
      print_Str("\n\r R1_ERROR");
      break;
    case READ_SUCCESS:
      print_Str("\n\r READ_SUCCESS");
      break;
    case START_TOKEN_TIMEOUT:
      print_Str("\n\r START_TOKEN_TIMEOUT");
      break;
    default:
      print_Str("\n\r UNKNOWN RESPONSE");
  }
}

/*
 * ----------------------------------------------------------------------------
 *                                                            PRINT WRITE ERROR
 * 
 * Description : Print Write Error Flag returned by a SD card read function.  
 * 
 * Arguments   : err     Write Error Response.
 * 
 * Returns     : void
 * ----------------------------------------------------------------------------
 */
void sd_PrintWriteError(uint16_t err)
{
  // 0xFF00 filters out the lower byte which is the R1 response
  switch(err & 0xFF00)
  {
    case DATA_ACCEPTED_TOKEN_RECEIVED:
      print_Str("\n\r DATA_ACCEPTED_TOKEN_RECEIVED");
      break;
    case CRC_ERROR_TOKEN_RECEIVED:
      print_Str("\n\r CRC_ERROR_TOKEN_RECEIVED");
      break;
    case WRITE_ERROR_TOKEN_RECEIVED:
      print_Str("\n\r WRITE_ERROR_TOKEN_RECEIVED");
      break;
    case INVALID_DATA_RESPONSE:
      print_Str("\n\r INVALID_DATA_RESPONSE");
      break;
    case DATA_RESPONSE_TIMEOUT:
      print_Str("\n\r DATA_RESPONSE_TIMEOUT");
      break;
    case CARD_BUSY_TIMEOUT:
      print_Str("\n\r CARD_BUSY_TIMEOUT");
      break;
    case R1_ERROR:
      print_Str("\n\r R1_ERROR");
      break;
    default:
      print_Str("\n\r UNKNOWN RESPONSE");
  }
}


/*
 * ----------------------------------------------------------------------------
 *                                                            PRINT ERASE ERROR
 * 
 * Description : Print Erase Error Flag returned by a SD card read function.  
 * 
 * Arguments   : err     Erase Error Response.
 * 
 * Returns     : void
 * ----------------------------------------------------------------------------
 */
void sd_PrintEraseError(uint16_t err)
{
  // 0xFF00 filters out the lower byte which is the R1 response
  switch(err & 0xFF00)
  {
    case ERASE_SUCCESSFUL:
      print_Str("\n\r ERASE_SUCCESSFUL");
      break;
    case SET_ERASE_START_ADDR_ERROR:
      print_Str("\n\r SET_ERASE_START_ADDR_ERROR");
      break;
    case SET_ERASE_END_ADDR_ERROR:
      print_Str("\n\r SET_ERASE_END_ADDR_ERROR");
      break;
    case ERASE_ERROR:
      print_Str("\n\r ERROR_ERASE");
      break;
    case ERASE_BUSY_TIMEOUT:
      print_Str("\n\r ERASE_BUSY_TIMEOUT");
      break;
    default:
      print_Str("\n\r UNKNOWN RESPONSE");
  }
}
