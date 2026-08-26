# Astro Stream MVP

Objetivo: exibir a tela do PS5 dentro do Astro Remote sem transformar o ELF em um cliente Remote Play pesado.

## Arquitetura

```text
PS5
  |
  | Remote Play
  v
Astro Stream Bridge (Linux / Docker)
  |
  | WebRTC
  v
Navegador
  |
  v
Astro Remote /screen
```

O ELF `astro_remote.elf` continua rodando no PS5 e servindo o painel na porta 45821. O processamento de Remote Play, decodificacao e WebRTC fica em um bridge externo na mesma rede do console.

Para o primeiro MVP o bridge usa o projeto open source `o1298098/remote-play`, distribuido sob licenca MIT. O Astro apenas incorpora a interface web do bridge na rota `/screen`.

## O que entrou nesta etapa

- rota autenticada `/screen` no Astro Remote;
- botao flutuante `ESPELHAR PS5` no dashboard;
- configuracao da URL do bridge salva em `localStorage` no navegador;
- modo tela cheia;
- fallback para abrir o bridge em nova aba;
- Compose do bridge em `stream/bridge/docker-compose.yml`.

## Subir o bridge

No computador Linux que esteja na mesma LAN do PS5:

```bash
cd stream/bridge
cp .env.example .env
```

Edite `.env` e ajuste principalmente:

- `ASTRO_STREAM_LAN_PARENT`: interface fisica do host, por exemplo `enp3s0`;
- `ASTRO_STREAM_LAN_SUBNET`: sub-rede do PS5;
- `ASTRO_STREAM_LAN_GATEWAY`: gateway da LAN;
- `ASTRO_STREAM_LAN_IP`: IP livre reservado ao backend do bridge.

Depois:

```bash
docker compose up -d
```

Interface web padrao:

```text
http://IP_DO_HOST:10110
```

API padrao:

```text
http://IP_DO_HOST:10111
```

## Primeira configuracao do Remote Play

1. Abra a interface do bridge.
2. Registre o PS5 usando o PIN de Remote Play exibido pelo console.
3. Em Settings > WebRTC Settings, configure `Public IP` como um endereco que o navegador consiga alcancar.
4. Confirme que o stream funciona diretamente pelo bridge antes de testar dentro do Astro.

## Conectar pelo Astro

1. Abra o Astro Remote.
2. Clique em `ESPELHAR PS5`.
3. Informe, por exemplo:

```text
http://192.168.0.20:10110
```

4. Clique em `Conectar`.

O endereco fica salvo somente no navegador atual.

## Limitacoes do MVP

- o bridge ainda e um servico externo;
- a interface upstream pode impedir uso em iframe por headers de seguranca. Se isso ocorrer, use `Abrir em nova aba` enquanto fazemos a integracao WebRTC nativa do Astro;
- acesso remoto atraves da Internet exige configuracao adequada de WebRTC/TURN e firewall;
- este passo ainda nao integra Gamepad API diretamente ao Astro.

## Proxima etapa

Depois de validar imagem e latencia:

1. mapear a API/signaling WebRTC do bridge;
2. substituir o iframe por um `<video>` controlado diretamente pelo Astro;
3. adicionar Gamepad API e encaminhar os comandos ao bridge;
4. integrar qualidade, bitrate, resolucao e metricas de latencia ao dashboard.
