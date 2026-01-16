# SRIX4K MyKey Implementation

## Overview

This implementation adds complete SRIX4K MyKey card management functionality to the SRIX Tool, based on the MIKAI/MyKey library. The MyKey system enables management of vendor codes, credit balances, and transaction history on SRIX4K cards.

## Features Implemented

### 1. Cryptographic Functions
- **encodeDecodeBlock()**: XOR-based bit swapping algorithm for data obfuscation
- **calculateBlockChecksum()**: Checksum calculation (0xFF - blockNum - sum of nibbles)
- **calculateEncryptionKey()**: Computes SK = UID × Vendor × OTP

### 2. Vendor Management
- **importVendor()**: Imports vendor code to blocks 0x18, 0x19, 0x1C, 0x1D
- **exportVendor()**: Exports current vendor code
- **isReset()**: Checks if key is in factory reset state

### 3. Credit Management
- **getCurrentCredit()**: Reads credit from block 0x21
- **addCents()**: Adds credit with automatic transaction logging
- **setCents()**: Sets specific credit amount (reset + add)

### 4. Security & Validation
- **checkLockID()**: Verifies Lock ID protection (block 0x05)
- **getCurrentTransactionOffset()**: Gets transaction pointer (block 0x3C)

### 5. Utilities
- **daysDifference()**: Calculates days from 1/1/1995
- **resetKey()**: Full factory reset to default values

### 6. User Interface
Six new menu options (visible when a dump is loaded):
- **MyKey Info**: Display vendor, credit, lock status, reset state
- **Add Credit**: Add credit to the card
- **Set Credit**: Set specific credit amount
- **Import Vendor**: Import a vendor code
- **Export Vendor**: View current vendor code
- **Reset Key**: Factory reset with confirmation

## SRIX4K Block Layout

```
0x05: Lock ID (0x7F = locked)
0x06: OTP (One Time Programmable)
0x07: Key ID
0x08: Production date (BCD)
0x18-0x19: Primary vendor code (encoded)
0x1C-0x1D: Backup vendor code (encoded)
0x21: Current credit + date (encoded)
0x25: Previous credit + date (encoded)
0x23, 0x27: Previous credit blocks
0x34-0x3B: Transaction history (8 slots)
0x3C: Transaction pointer (0-7)
```

## Factory Reset Values

```c
block18Reset = 0x8FCD0F48
block19Reset = 0xC0820007
```

## Usage Workflow

### Reading and Viewing MyKey Information

1. **Read a tag**: Select "Read tag" from the main menu
2. **View MyKey Info**: Select "MyKey Info" to see:
   - Vendor code (hexadecimal)
   - Current credit balance (EUR and cents)
   - Lock ID status (LOCKED/UNLOCKED)
   - Reset state (FACTORY RESET/CONFIGURED)

### Managing Credit

1. **Add Credit**:
   - Select "Add Credit"
   - Enter amount in cents (e.g., 500 for 5.00 EUR)
   - Credit is added to current balance
   - Transaction is logged with timestamp

2. **Set Credit**:
   - Select "Set Credit"
   - Enter new total credit in cents
   - Previous credit is reset, new credit is set
   - Transaction is logged

### Managing Vendor Code

1. **Export Vendor**:
   - Select "Export Vendor"
   - View current vendor code in hex and decimal

2. **Import Vendor**:
   - Select "Import Vendor"
   - Enter vendor code in hexadecimal
   - Vendor is stored in both primary (0x18-0x19) and backup (0x1C-0x1D) locations

### Factory Reset

1. Select "Reset Key"
2. Review warning message
3. Choose "Confirm Reset" or "Cancel"
4. If confirmed:
   - Vendor reset to factory default (0x8FCD0F48, 0xC0820007)
   - Credit reset to 0
   - Transaction history cleared
   - Transaction pointer reset

### Writing Changes to Card

After making any modifications:
1. Press [OK] to return to main menu
2. Select "Clone tag" or "Write to tag"
3. Place a writable SRIX4K card on reader
4. Wait for write operation to complete

## Data Formats

### Vendor Code
- Stored as 32-bit unsigned integer
- Actual value stored is (vendor - 1)
- Split across two blocks and encoded
- Duplicated in primary and backup locations

### Credit
- Stored in cents (e.g., 500 = 5.00 EUR)
- Upper 16 bits of block: credit amount
- Lower 16 bits of block: date stamp (days since 1/1/1995)
- Block is encoded using encodeDecodeBlock algorithm

### Encryption Key
- SK = UID × Vendor × OTP
- UID: 64-bit card unique identifier
- Vendor: 32-bit vendor code (decoded)
- OTP: 32-bit one-time programmable value (byte-swapped, two's complement)

## Technical Details

### Encode/Decode Algorithm
The `encodeDecodeBlock()` function performs XOR-based bit swapping using a specific pattern. It's applied three times with different masks to obfuscate data. The function is its own inverse (applying it twice returns the original value).

### Date Calculation
Dates are stored as days elapsed since January 1, 1995. The `daysDifference()` function accounts for leap years.

### Transaction History
Up to 8 transactions are stored in a circular buffer (blocks 0x34-0x3B). The pointer in block 0x3C (0-7) indicates the next transaction slot.

## Testing Checklist

- [ ] Read a SRIX4K tag successfully
- [ ] View MyKey Info displays correct vendor and credit
- [ ] Export Vendor shows expected value
- [ ] Import Vendor updates blocks correctly
- [ ] Add Credit increases balance properly
- [ ] Set Credit replaces balance correctly
- [ ] Reset Key restores factory defaults
- [ ] Write modified dump to new card
- [ ] Verify written card reads back correctly

## Security Considerations

1. **Lock ID**: Check lock status before attempting modifications
2. **Backup**: Always save dumps before making changes
3. **Verification**: Read card after writing to verify changes
4. **Vendor Code**: Importing incorrect vendor may break card functionality

## References

- Based on MIKAI/MyKey library implementation
- SRIX4K datasheet for block structure
- Original implementation: mikai.c and mykey.c

## Files Modified

- `src/modules/rfid/srix_tool.h`: Added MyKey function declarations and member variables
- `src/modules/rfid/srix_tool.cpp`: Implemented all MyKey functionality (~650 lines added)

## Known Limitations

1. Date uses current system time (ensure device clock is set correctly)
2. Transaction splitting feature planned but simplified in current implementation
3. Checksum calculation implemented but not currently validated during operations

## Future Enhancements

- Transaction splitting for large amounts (2€, 1€, 0.50€, 0.20€, 0.10€, 0.05€ denominations)
- Enhanced validation with checksum verification
- Transaction history viewer
- Bulk card programming interface
- Vendor database management
