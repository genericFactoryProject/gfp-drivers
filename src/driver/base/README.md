# core flow


bus
bus_register



driver
driver_register




device
device_register




platform device:




[boot] parse dtb:
start_kernel ->
    steup_arch ->
        setup_machine_fdt/unflatten_device_tree(create tree of device_nodes from flat blob -> )
            unflatten_dt_nodes ->
                populate_node ->
                    populate_properties

bootline:
setup_machine_fdt ->
    early_init_dt_scan ->
        early_init_dt_scan_nodes ->
            early_init_dt_scan_chosen(boot_command_line)

[arch_initcall_sync] platform device register:
of_platform_default_populate_init ->
    of_platform_default_populate ->
        of_platform_bus_create ->
            of_platform_device_create_pdata ->
                of_device_alloc ->
                    platform_device_alloc


supplier/consumer? brother


of parse:

of_node_init ->
    fwnode_init ->
        of_fwnode_ops

of_fwnode_ops ->
    of_fwnode_add_links ->
        of_link_property ->
            of_supplier_bindings

of_supplier_bindings ->


address:


device boot depend:
1. component x
2. delay probe x
3. fwnode link


compat bus:
1. container x
2. cpu x
3. memory x
4. node x
5. soc x
6. platform



