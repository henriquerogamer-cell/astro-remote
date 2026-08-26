# Astro Remote Split Service

## Objetivo

O Astro continua sendo o unico ponto exposto. O Remote roda em um processo filho isolado e escuta somente no loopback do PS5.

```text
Internet / navegador
        |
        v
astro_remote.elf / astrormt
0.0.0.0:45821
Astro, painel, auth e supervisor
        |
        | localhost
        v
astrorem
127.0.0.1:45822
Remote, video e controle
```

## Regras da arquitetura

- `45821` pertence ao Astro e pode ser exposta externamente depois da autenticacao final;
- `45822` pertence ao Remote e faz bind somente em `127.0.0.1`;
- o navegador nunca precisa acessar `45822` diretamente;
- iniciar Remote cria um processo filho separado com `fork()`;
- o filho fecha os descritores herdados antes de abrir `45822`, portanto nao mantem `45821` presa;
- o Astro acompanha o PID do filho, faz healthcheck em `127.0.0.1:45822` e coleta o processo com `waitpid()`;
- encerrar Remote tenta shutdown cooperativo, depois `SIGTERM`, depois `SIGCONT` + `SIGKILL`;
- se o filho ficar preso mesmo apos `SIGKILL`, o Astro continua vivo em `45821` e registra `ki_stat`, `ki_wmesg` e `ki_lockname` para diagnostico;
- um zumbi real e recolhido pelo Astro pai via `waitpid()`;
- nenhum bridge externo ou PC intermediario faz parte desta arquitetura.

## Build experimental 0.15

Este build valida primeiro o isolamento de processos e das duas portas. O worker `astrorem` usa apenas o probe seguro/passivo da fonte Remote Play. A chamada nativa potencialmente bloqueante nao voltou ainda. Depois de confirmar no PS5 que `fork()`, `45822` e o supervisor funcionam, a inicializacao nativa de Remote Play pode ser testada dentro do worker sem arriscar o servidor principal do Astro.
