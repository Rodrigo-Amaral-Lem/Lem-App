> Atualização 0.1.23: a identificação Android atual é `pt.lem.app`, uma instalação separada. Sincronizar trabalho pendente antes de trocar. As notas de compatibilidade desta página descrevem a entrega histórica.

# LEM 0.1.7 — remover fotografias de rascunhos

Junto a cada fotografia de um report editável existe agora **Remover**. A app pede confirmação e identifica o item. Cancelar conserva a imagem. A remoção elimina a associação ao report e a cópia privada da app; o ficheiro original importado da galeria não é apagado. Fotografias de reports concluídos não podem ser removidas.

A operação funciona sem rede. A remoção fica guardada numa fila local, inclusive se o upload anterior não foi confirmado. Na próxima sincronização é enviada ao servidor antes de novas fotografias e antes do fecho do report. Se a app for reiniciada, a fila é recuperada. Uma falha de rede ou uma API antiga conserva a remoção pendente e o report por sincronizar. Se o report entretanto tiver sido fechado no servidor, a app assinala o conflito e não trata a remoção como concluída.

Quando a última fotografia de um item obrigatório é removida, o item volta a apresentar fotografia em falta e impede a conclusão, exceto quando a resposta é NA, conforme as regras existentes.

## Instalar

É necessário **atualizar o servidor e recompilar a app**. O pacote contém código fonte, não um APK.

1. Extrair o pacote completo e atualizar a API. No Windows, a partir da pasta `LEM`, executar `./deploy/deploy.ps1` no modo normal, **sem `-ResetDatabase`**. No laptop Linux, executar `bash deploy/deploy.sh`, também sem reset. Consultar `deploy-powershell.md` ou `deploy-linux.md` para a configuração SSH existente.
2. Recompilar `app/CMakeLists.txt` com o kit Qt Android/OpenSSL já configurado e instalar. Confirmar versão 0.1.7 em Conta. O versionCode Android é 107 e o identificador do pacote permanece `pt.lem.app`.
3. Testar com um rascunho: anexar fotografia, cancelar uma remoção, confirmar a remoção e sincronizar. Confirmar a ausência da imagem noutro cliente ou no ZIP/PDF atualizado.

Não há alteração de esquema da base de dados e não é necessário reset. O cliente anterior continua a usar os endpoints existentes. A API passa a identificar-se como 0.1.7 e acrescenta `DELETE /api/reports/{rid}/photos/{pid}?base_revision=<revisão>`.

A API verifica acesso ao contrato, autoria/administração e estado rascunho, valida a revisão e regista `photo.delete` na auditoria. A remoção e o avanço de revisão são atómicos; uma repetição depois de a fotografia já ter sido eliminada é idempotente. Eliminar uma fotografia de outro report é rejeitado.

## Validação

- Testes QML com backend fictício, em largura de desktop e telemóvel: botão Remover, Cancelar/confirmar, atualização dos requisitos de evidência, ausência da opção em reports concluídos e restantes interações da interface. Sem avisos QML.
- Cliente C++ compilado e executado com Qt 6.10.2 em Linux: remoção local, conservação do ficheiro original, eliminação da cópia privada, repetição sem duplicar a fila, recuperação da fila após reconstrução do backend, bloqueio de remoção num report fechado e de conclusão sem fotografia obrigatória.
- Falha deliberada de gravação local: a fotografia e a associação permanecem intactas; não é mostrada uma remoção bem-sucedida.
- Integração do cliente C++ com API HTTP local descartável: upload, sincronização do rascunho, remoção de uma fotografia já enviada, sincronização da remoção, fotografia de substituição, fecho e leitura por outro cliente.
- API: autorização, revisão antiga, fotografia de outro report, remoção efetiva, repetição idempotente, ausência em ZIP, bloqueio de fecho sem evidência e imutabilidade após fecho. Suite do servidor: 5 aprovados e 14 testes opcionais de ficheiro Excel externo ignorados.

Não foi compilado nem instalado um APK nesta entrega; falta validar a interação num dispositivo Android físico e a atualização no servidor do utilizador. Os ensaios de servidor usaram SQLite e dados fictícios; não foi executado um ensaio de concorrência PostgreSQL.
