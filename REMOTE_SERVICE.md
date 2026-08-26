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
- ao iniciar o Astro, o supervisor cria `astrorem` com `fork()` e deixa o worker residente em estado idle;
- o filho fecha os descritores herdados antes de abrir `45822`, portanto nao mantem `45821` presa;
- o botao Iniciar Remote inicia somente a sessao dentro de `astrorem`, sem criar outro processo;
- Encerrar Sessao encerra somente a sessao e mantem `astrorem` vivo na `45822`;
- o Astro acompanha o PID do filho, faz healthcheck real em `127.0.0.1:45822` com timeout e coleta o processo com `waitpid()`;
- se a sessao ou o worker parar de responder, o Astro tenta shutdown cooperativo, depois `SIGTERM`, depois `SIGCONT` + `SIGKILL`, recolhe o filho e pode criar um worker limpo;
- se o filho ficar preso mesmo apos `SIGKILL`, o Astro continua vivo em `45821` e registra `ki_stat`, `ki_wmesg` e `ki_lockname` para diagnostico;
- um zumbi real e recolhido pelo Astro pai via `waitpid()`;
- nenhum bridge externo ou PC intermediario faz parte desta arquitetura.

## Build experimental 0.15

Este build valida primeiro o isolamento de processos e das duas portas. Ao executar `astro_remote.elf`, devem existir `astrormt` em `45821` e `astrorem` em `127.0.0.1:45822` simultaneamente. O worker usa apenas o probe seguro/passivo da fonte Remote Play. A chamada nativa potencialmente bloqueante ainda nao foi reintroduzida.

Depois de confirmar no PS5 que `fork()`, o bind localhost da `45822`, o healthcheck e o supervisor funcionam, a inicializacao nativa de Remote Play pode ser testada dentro de `astrorem`. Assim, mesmo que o motor Remote trave, o painel principal do Astro continua respondendo na `45821`.
