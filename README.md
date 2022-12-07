
<!-- **Table of content** -->
<!-- - [AMP and DCM](#amp-and-dcm) -->
<!-- - [Installation](#installations) -->
<!--   - [Mac](#mac) -->
<!--   - [Linux](#linux) -->
<!-- - [Simulations](#simulations) -->
<!-- - [Contact](#contact) -->
<!-- - [How to cite this code-base?](#how-to-cite-this-code-base) -->

# AMP: An Adaptive Multipath TCP for Data Center Networks
    
## Linux

Versions Ubuntu20.4 with gcc/g++ 5 .

1. Configuration and building

``` shell
CXXFLAGS="-Wall" ./waf --build-profile=optimized configure build 
```

2. Run a simulation

``` shell
./waf --run "amp_model"
```

License

```
@article{kheirkhah2015multipath,
  author  = {Kheirkhah, Morteza and Wakeman, Ian and Parisis, George},
  title   = {{Multipath-TCP in ns-3}},
  journal = {CoRR},
  year    = {2015},
  url     = {http://arxiv.org/abs/1510.07721},
  archivePrefix = {arXiv},
}
```
