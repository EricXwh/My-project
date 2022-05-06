# Project 3 - Distance Vector Routing

---

To run the code, you need to put the topology and the codes in the same folder.
I use python3 in ubuntu.
You can run the code by typing: python DV.py <r_id> <r_port> <the location of configuration file>
For example: python DV.py v 12001 topology/configv.txt

To join the node, you just need to run another DV.py using another router.
For example: python DV.py u 12001 topology/configu.txt

To update the new cost, you can run the update.py by typing: python update.py
Then you need to enter the two node and the new cost that you want to change.
For example: u x 8


**Author:** wxx144
**Date:** May 5, 2022
