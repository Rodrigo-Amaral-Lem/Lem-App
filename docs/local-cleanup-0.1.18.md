> **Atualização 0.1.35 R1:** a limpeza manual também remove rascunhos totalmente sincronizados. As referências abaixo à proteção de todos os rascunhos descrevem a versão 0.1.18. Ver [comportamento atual](local-cleanup-0.1.35-R1.md).

# Limpeza local — 0.1.18

A opção redundante **Conta** foi retirada do menu do report. O ícone de conta mantém o acesso.

**Limpar cópias antigas** chamava a política automática: só removia fotografias antigas ou acima do limite de armazenamento, excluindo o report aberto e os mantidos offline. Foi substituída por **Limpar cópias locais**, com confirmação e resultado explícito.

A limpeza manual elimina imediatamente as cópias locais completas de reports concluídos e já confirmados no servidor, juntamente com as fotografias privadas da app. Inclui o report aberto, fotografias recentes e reports marcados para manter offline. Rascunhos, reports com alterações pendentes, reports nunca confirmados pelo servidor e fotografias ainda não enviadas ficam protegidos. Os originais na galeria mantêm-se.

Os reports continuam no catálogo porque existem no servidor. Ao reabrir um report limpo, os seus dados são obtidos do servidor; as fotografias ficam por descarregar até as pedir novamente. A limpeza local não usa a operação de apagar reports no servidor nem cria marcadores de apagamento definitivo. A rotina automática continua a respeitar o limite de 256 MiB, a idade e as marcações offline.

Para apagar definitivamente um report fechado e as suas fotografias do servidor, é necessária uma conta de administrador e a opção **Apagar report fechado…** no menu do report, com servidor 0.1.15 ou posterior. Uma falha nessa operação conserva a cópia local e apresenta a mensagem do servidor.

Verificação: testes QML de remoção da opção redundante, confirmação e cancelamento, em desktop e dimensões Android. Cliente C++: limpeza imediata de cópia recente, aberta e mantida offline; conservação de rascunhos, alterações pendentes e originais; persistência após reinício. Integração HTTP local: limpeza de report com quatro fotografias e reabertura do servidor sem caminhos de imagens locais, seguida das regressões existentes. Testes locais usam Qt 6.10.2; não foi executada compilação Android nesta revisão.

Atualizar apenas a app para esta correção. O servidor mantém a versão 0.1.15.
