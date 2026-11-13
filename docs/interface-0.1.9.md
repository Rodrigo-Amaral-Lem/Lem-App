> Atualização 0.1.23: a identificação Android atual é `pt.lem.app`, uma instalação separada. Sincronizar trabalho pendente antes de trocar. As notas de compatibilidade desta página descrevem a entrega histórica.

# LEM 0.1.9 — fotografias offline e nomes do servidor

## Descarregar todas as fotografias de um report

No report, abrir o menu superior e escolher **Descarregar todas as fotografias**. A confirmação indica a quantidade em falta e o tamanho conhecido da transferência. Se a API antiga não fornecer tamanhos, a app indica essa limitação em vez de apresentar uma estimativa incorreta.

- Obtém apenas imagens que ainda não estejam guardadas no smartphone. Fotografias já existentes e fotografias locais ainda por enviar não são transferidas novamente.
- Há no máximo dois downloads em simultâneo, partilhados com os downloads individuais. O formulário e a navegação continuam disponíveis. O progresso identifica o report de origem.
- **Cancelar fotografias restantes** impede novos pedidos; os pedidos em curso terminam. As imagens já recebidas ficam guardadas.
- Uma interrupção conserva o que já foi descarregado. Repetir a ação obtém apenas o que continua em falta. Fotografias removidas durante a transferência não são novamente anexadas.
- O report fica marcado para **manter fotografias offline**, incluindo depois de reiniciar a app. Estas cópias ficam fora da limpeza automática por idade/quota. Esta escolha explícita pode ultrapassar o objetivo de cache de 256 MiB; o diálogo avisa que o espaço ficará reservado.
- Para voltar à política normal, escolher **Permitir limpeza destas fotografias** no menu. Não apaga fotografias do servidor nem originais da galeria; volta a permitir a limpeza segura das cópias locais de reports concluídos/sincronizados. Rascunhos e envios pendentes continuam protegidos.
- Fotografias locais em falta que nunca tenham sido enviadas não podem ser recuperadas do servidor. A app assinala essa situação.

O servidor passa a devolver o tamanho das fotografias nos metadados sem carregar os ficheiros binários completos na memória apenas para mostrar o report. Os endpoints anteriores continuam disponíveis.

## Nomes técnicos do servidor

A instalação ativa passa a usar:

| Elemento | Nome |
|---|---|
| Projeto Docker Compose | `lem` |
| Base de dados e utilizador PostgreSQL | `lem` |
| Utilizador da API dentro do container | `lem` |
| Base SQLite de desenvolvimento | `lem.db` |
| Variável do domínio | `LEM_DOMAIN` |
| Variáveis locais dos scripts shell | prefixo `lem_` |
| Backups novos | prefixo `lem-` |

A palavra portuguesa “atributo” no sentido de caixa de formulário não é um nome do produto e mantém-se nas validações de importação. O identificador Android e as classes internas do cliente não mudam nesta entrega, para conservar os dados privados já existentes no smartphone.

## Primeira atualização da instalação antiga

**A mudança de nomes do PostgreSQL exige recriar a base de dados**, conforme autorizado. A opção de reset é explícita: o deploy normal recusa esta migração antes de parar serviços ou apagar dados. Executar no laptop pessoal, dentro do projeto completo extraído.

Linux:

```bash
bash deploy/deploy.sh --reset-database
```

Se necessário:

```bash
bash deploy/deploy.sh --reset-database --key ~/.ssh/lem_server
```

PowerShell:

```powershell
.\deploy\deploy.ps1 -ResetDatabase
```

O reset apaga utilizadores, sessões, contratos, carregadores, reports, fotografias, importações e auditoria da base desta instalação. O administrador é recriado com `ADMIN_EMAIL` e **a password inicial `ADMIN_PASSWORD` do `.env`**, que pode diferir de uma password alterada na aplicação. Não é enviada nem impressa pelo script.

Depois: sair e voltar a entrar na app, criar os contratos/técnicos necessários e voltar a importar os carregadores. Rascunhos antigos referem IDs eliminados e não devem ser reenviados para a base nova. Os ficheiros privados do smartphone não são apagados automaticamente.

Não substituir manualmente o `compose.yml` ou o `.env` no servidor antes de executar esta migração: o script lê a configuração instalada para identificar os recursos antigos. O projeto novo é enviado a partir do laptop.

## O que o deploy conserva e verifica

1. Identifica o projeto Compose, o volume PostgreSQL e os volumes HTTPS da instalação existente.
2. Prepara um `.env` atualizado fora do contexto de build Docker, com permissões 0600. Normaliza a chave antiga de domínio para `LEM_DOMAIN`, fixa `COMPOSE_PROJECT_NAME=lem` e conserva as passwords/email/domínio. O conteúdo privado não é impresso.
3. Confirma a existência dos volumes HTTPS e constrói a API antes de parar a instalação antiga.
4. Recusa colidir com uma outra instalação que já use o projeto ou volume `lem`. Antes de apagar a base antiga, verifica os rótulos Docker de projeto e volume.
5. No reset, remove apenas o volume PostgreSQL validado. Se o projeto tiver mudado, remove também os containers antigos desta instalação, incluindo o proxy, para libertar as portas HTTPS.
6. Reutiliza os volumes de certificados como volumes externos, através de `LEM_TLS_DATA_VOLUME` e `LEM_TLS_CONFIG_VOLUME`. Os seus nomes físicos podem conservar uma identificação histórica; não são apagados nem se força uma nova emissão de certificados.
7. Guarda o código/configuração anterior em `.deploy-backups`, incluindo uma cópia privada do `.env`. Estes arquivos históricos podem conter nomes antigos; não são configuração ativa.
8. Instala o código, aplica o novo `.env`, inicia o projeto `lem` e verifica HTTPS. Não há rollback automático nem recuperação da base apagada no modo reset.

A instalação inicial Ubuntu foi adaptada para criar os volumes HTTPS externos antes de iniciar o Compose. O backup normal passa a usar a base/utilizador `lem`.

## Atualizações seguintes

Depois de concluída a migração, voltar ao comando normal para conservar a base de dados e obter backup automático:

```bash
bash deploy/deploy.sh
```

O modo de reset **não** passou a ser o comportamento por defeito dos deploys futuros.

## App e validação

Recompilar a app com o kit Qt/OpenSSL habitual e confirmar **0.1.9** em Conta; versionCode Android 109. Este pacote contém código fonte, não um APK.

Validado localmente, sem ligação nem alterações ao servidor do utilizador:

- QML: diálogo de confirmação, estado de progresso e formulário editável durante os downloads, em largura de telemóvel e desktop.
- C++/Qt 6.10.2 com API HTTP descartável: download em lote, imagens já em cache não repetidas, dois pedidos simultâneos no máximo, navegação para outra intervenção durante o download, cancelamento dos pedidos restantes, interrupção simulada e repetição apenas do que falta. Foram observados 13 GET de imagens para a sequência completa de cenários, incluindo uma falha deliberada.
- Cache: marcação offline persiste e protege cópias confirmadas mesmo acima da quota; ao libertar a marcação, a limpeza normal pode voltar a removê-las. Fotografias pendentes/rascunhos permanecem protegidos.
- API: tamanho correto nos metadados e consulta sem obter o BLOB inteiro.
- Deploy com SSH/Docker simulados: migração antiga com reset, recusa sem reset, conflito com volume de outra instalação, rótulos inválidos e falha de build antes da paragem; normalização do `.env`, conservação dos valores privados e referências aos certificados. O cliente PowerShell não foi executado nesta entrega; reutiliza o mesmo processo remoto testado em Linux.

Falta validar o APK num Android físico e a migração com Docker/PostgreSQL/Caddy reais no servidor. Nenhum reset foi executado remotamente nesta entrega.
