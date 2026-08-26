# Astro Remote Resident Service

## Objetivo

O Astro Remote roda como um servico residente dentro do PS5. O navegador e apenas a interface de controle. Nenhum bridge externo, PC intermediario ou URL adicional faz parte da arquitetura final.

```text
Prospero autoload
      |
      v
astro_remote.elf
      |
      +-- HTTP :45821
      +-- /remote
      +-- /api/remote/status
      +-- /api/remote/start
      +-- /api/remote/stop
      |
      v
Browser em PC / tablet / celular
```

## Estado atual

A versao 0.14.0 cria o modulo Remote como parte do processo residente do Astro.

- o servico Remote nasce junto com `astro_remote.elf`;
- fechar a aba do navegador nao encerra o servico;
- uma sessao pode ser iniciada e encerrada pela API;
- o estado da sessao fica no processo do Astro;
- `/screen` continua como alias de `/remote` durante a transicao;
- nao existe dependencia de bridge externo;
- a fonte de video ainda nao esta conectada nesta etapa.

## Proxima etapa

Conectar a fonte de video do proprio PS5 ao modulo residente:

```text
PS5 video / Remote Play source
          |
          v
Astro Remote Service
          |
          v
stream para o browser
```

Depois entram audio, transporte de baixa latencia e controle via navegador.
