# Squeeze

LZ77, Huffman dictionary compression

### Based on:

https://en.wikipedia.org/wiki/LZ77_and_LZ78

https://en.wikipedia.org/wiki/Huffman_coding

https://en.wikipedia.org/wiki/LHA_(file_format)

### Goals:

* Simplicity.
* Ease of build and use.

### No goals:

* Performance (both CPU and memory).
* Existing archivers compatibility.
* Stream encoding decoding.

### Test materials:

Because Chinese texts are very compact comparing to e.g. the KJV bible
the Guttenberg License wording is stripped from the text files.

* See downloads.bat

### Further development (if productizing ever needed):

* Remove map and huffman interfaces to allow compiler to do deep inline.
* Amalgamate all source into single header file.

