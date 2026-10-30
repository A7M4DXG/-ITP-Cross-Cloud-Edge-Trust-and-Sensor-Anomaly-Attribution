# Azure deployment and operation

Current architecture: one XIAO Azure gateway and three XIAO sensor nodes (Nodes 1-2: DHT11; Node 3: DHT22 configuration).

- [Operator guide: networking, approval, quota, diagnostics and hardware acceptance](../../documentation/setup/azure-operations.md)
- [Dataset access handover for the data-science teammate](../../documentation/setup/azure-dataset-access.md)
- [Architecture diagram source](../../diagrams/azure-three-sensor-flow.mmd)
- [Diagnostics queries](diagnostics/queries.kql)

Raw data: crosscloudresearch01 / research-raw.
Monitoring: law-crosscloud-research / diag-crosscloud-research.
Cloud device: azure-gateway-01 on iot-hub-crosscloud, F1.

Deployment state must be verified: cloud diagnostics are enabled and approval
defaults are denied. New firmware features require uploading the changed
gateway and all three sensor sketches and completing the physical acceptance
check. Do not describe a local build as a successful live three-node test.

The gateway still uses SAS/TLS. X.509 client-certificate authentication remains
a separate target security milestone.
