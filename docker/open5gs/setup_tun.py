#!/usr/bin/env python3

import click
import ipaddress
import subprocess
import logging
from pyroute2 import IPRoute
from pyroute2.netlink import NetlinkError


def handle_ip_string(ctx, param, value):
    try:
        ret = ipaddress.ip_network(value)
        return ret
    except ValueError:
        raise click.BadParameter(f"{value} is not a valid IP range.")


def iptables_add_masquerade(if_name, ip_range):
    # Use system iptables to avoid python-iptables backend issues (nft vs legacy)
    cmd = [
        "iptables",
        "-t",
        "nat",
        "-A",
        "POSTROUTING",
        "-s",
        ip_range,
        "-o",
        if_name,
        "-j",
        "MASQUERADE",
    ]
    logging.info("Adding iptables NAT MASQUERADE: %s", " ".join(cmd))
    try:
        subprocess.run(cmd, check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as e:
        logging.error("Failed to add MASQUERADE rule: %s", e.stderr or e.stdout)
        raise


def iptables_allow_all(if_name):
    cmd = [
        "iptables",
        "-A",
        "INPUT",
        "-i",
        if_name,
        "-j",
        "ACCEPT",
    ]
    logging.info("Adding iptables INPUT ACCEPT: %s", " ".join(cmd))
    try:
        subprocess.run(cmd, check=True, capture_output=True, text=True)
    except subprocess.CalledProcessError as e:
        logging.error("Failed to add INPUT ACCEPT rule: %s", e.stderr or e.stdout)
        raise


@click.command()
@click.option("--if_name", default="ogstun", help="TUN interface name.")
@click.option(
    "--ip_range",
    default="10.45.0.0/24",
    callback=handle_ip_string,
    help="IP range of the TUN interface.",
)
def main(if_name, ip_range):

    logging.basicConfig(level=logging.INFO, format="[setup_tun] %(levelname)s: %(message)s")
    logging.info("Configuring TUN '%s' with range '%s'", if_name, ip_range.with_prefixlen)

    # Get the first usable host IP and netmask prefix length
    host_addr = next(ip_range.hosts(), None)
    if host_addr is None:
        logging.error("Invalid IP range: %s", ip_range)
        raise ValueError("Invalid IP range.")

    first_ip_addr = str(host_addr)
    ip_netmask = ip_range.prefixlen

    ipr = IPRoute()
    # create the tun interface (if it already exists, ignore the error)
    try:
        logging.info("Creating TUN interface: %s", if_name)
        ipr.link("add", ifname=if_name, kind="tuntap", mode="tun")
    except NetlinkError as e:
        logging.info("TUN interface %s already exists or cannot be created: %s", if_name, e)

    # lookup the index
    dev = ipr.link_lookup(ifname=if_name)[0]
    # bring it down
    logging.info("Bringing interface down: index=%s", dev)
    ipr.link("set", index=dev, state="down")
    # add primary IP address (ignore if it already exists)
    try:
        logging.info("Assigning IP %s/%s to %s", first_ip_addr, ip_netmask, if_name)
        ipr.addr("add", index=dev, address=first_ip_addr, mask=ip_netmask)
    except NetlinkError as e:
        logging.info("IP address already present or failed to add: %s", e)
    # bring it up
    logging.info("Bringing interface up: index=%s", dev)
    ipr.link("set", index=dev, state="up")

    try:
        logging.info("Adding route: dst=%s via %s", ip_range.with_prefixlen, first_ip_addr)
        ipr.route("add", dst=ip_range.with_prefixlen, gateway=first_ip_addr)
    except NetlinkError as e:
        logging.info("Route exists or failed to add: %s", e)

    # setup iptables using system commands to avoid python-iptables issues
    iptables_add_masquerade(if_name, ip_range.with_prefixlen)
    iptables_allow_all(if_name)
    logging.info("TUN setup completed successfully for %s", if_name)


if __name__ == "__main__":
    main()
