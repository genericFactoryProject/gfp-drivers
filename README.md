# wells for gf kernel

## overview

The gf references a resource view framework as shown below:
![](doc/gf-framework.png)

## Inheritance

1. base(kobj/kset) -> driver -> device|bus|driver -> vfs|sysfs; Compared with the Linux driver framework, it is simpler
2. base(kobj/kset) -> object   -> ko|xnet|call
3. base(kobj/kset) -> runtime -> thread|channel|mod
