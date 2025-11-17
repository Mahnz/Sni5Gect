#!/usr/bin/env python3

import click
import ipaddress
import iptc
from pyroute2 import IPRoute
from pyroute2.netlink import NetlinkError
import subprocess


def handle_ip_string(ctx, param, value):
    try:
        ret = ipaddress.ip_network(value)
        return ret
    except ValueError:
        raise click.BadParameter(f"{value} is not a valid IP range.")


def iptables_add_masquerade(if_name, ip_range):
    chain = iptc.Chain(iptc.Table(iptc.Table.NAT), "POSTROUTING")
    rule = iptc.Rule()
    rule.src = ip_range
    rule.out_interface = if_name
    target = iptc.Target(rule, "MASQUERADE")
    rule.target = target
    chain.insert_rule(rule)


def iptables_allow_all(if_name):
    chain = iptc.Chain(iptc.Table(iptc.Table.FILTER), "INPUT")
    rule = iptc.Rule()
    rule.in_interface = if_name
    target = iptc.Target(rule, "ACCEPT")
    rule.target = target
    chain.insert_rule(rule)


@click.command()
@click.option("--if_name", default="ogstun", help="TUN interface name.")
@click.option(
    "--ip_range",
    default="10.45.0.0/24",
    callback=handle_ip_string,
    help="IP range of the TUN interface.",
)
def main(if_name, ip_range):

    # Execute and Return the output of iptables -t nat -L
    result = subprocess.run(
        ["iptables", "-t", "nat", "-L"], capture_output=True, text=True
    )
    print("NAT table rules:")
    print(result.stdout)
    if result.stderr:
        print("Errors:", result.stderr)

    for subnet in range(0, 256):
        # Get the first IP address in the IP range and netmask prefix length
        first_host = next(ip_range.hosts(), None)
        if not first_host:
            raise ValueError("Invalid IP range.")
        first_ip_addr = first_host + (subnet * 256)
        first_ip_addr = first_ip_addr.exploded

        ip_netmask = ip_range.prefixlen

        ipr = IPRoute()
        # create the tun interface
        ipr.link("add", ifname=if_name, kind="tuntap", mode="tun")
        # lookup the index
        dev = ipr.link_lookup(ifname=if_name)[0]
        # bring it down
        ipr.link("set", index=dev, state="down")
        # add primary IP address
        ipr.addr("add", index=dev, address=first_ip_addr, mask=ip_netmask)
        # bring it up
        ipr.link("set", index=dev, state="up")

        try:
            ipr.route("add", dst=ip_range.with_prefixlen, gateway=first_ip_addr)
        except NetlinkError:
            pass

        # setup iptables (best-effort). If iptables backend/modules are not available
        # inside the container, don't crash the entrypoint; just continue.
        try:
            iptables_add_masquerade(if_name, ip_range.with_prefixlen)
            iptables_allow_all(if_name)
        except Exception as e:
            print(f"Failed to configure iptables rules: {e}")
            print("Proceeding without container-managed NAT/ACCEPT rules. Ensure host/networking handles routing if needed.")
        # 'iptables -t nat -A POSTROUTING -s ' + ip_range.with_prefixlen + ' ! -o ' + if_name + ' -j MASQUERADE'

        # 'iptables -A INPUT -i ' + if_name + ' -j ACCEPT'


if __name__ == "__main__":
    main()
