# About

The goal of this project is to allow users to convert hashcat's WPA/WPA2 .hccapx files (back) to (p)cap files.

# Requirements

Software:  
- gcc compiler
- aircrack-ng/wireshark etc to open/test the generated .cap file


# Installation and First Steps

* Clone this repository:  
    git clone https://github.com/philsmd/hccapx2cap.git
* Compile it:  
    cd hccapx2cap   
    gcc -o hccapx2cap hccapx2cap.c
* Get a test .hccapx file (e.g):   
    wget http://hashcat.net/misc/example_hashes/hashcat.hccapx
* Run it:   
    ./hccapx2cap hashcat.hccapx hashcat_converted.cap 
* Check results:  
    wireshark hashcat_converted.cap
 
# Hacking

* Simplify code
* Make it easier readable
* Improve message generation
* CLEANUP the code, use more coding standards, everything is welcome (submit patches!)
* all bug fixes are welcome
* testing with huge set of hccapx files
* solve and remove the TODOs
* and,and,and

# Credits and Contributors 
Credits go to:  
  
* philsmd, hashcat project, aircrack-ng suite

# Project announcement/read also

[hccap2cap](https://github.com/philsmd/hccap2cap/)  
[project announcement of hccap2cap on the hashcat forum ](https://hashcat.net/forum/thread-2284-post-13717.html#pid13717)  

# License/Disclaimer

License: belongs to the PUBLIC DOMAIN, donated to hashcat, credits MUST go to hashcat and philsmd for their hard work. Thx  
  
Disclaimer: WE PROVIDE THE PROGRAM “AS IS” WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE Furthermore, NO GUARANTEES THAT IT WORKS FOR YOU AND WORKS CORRECTLY
