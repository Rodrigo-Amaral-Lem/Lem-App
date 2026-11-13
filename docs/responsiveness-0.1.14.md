# Resposta aos toques — 0.1.14

O preenchimento de respostas e observações deixa de esperar pela gravação atómica no armazenamento. A interface atualiza de imediato e um único trabalhador grava os diários dos reports em segundo plano. Alterações rápidas do mesmo report são agrupadas, mantendo a versão mais recente. A fila começa após uma janela curta de 20 ms.

O estado mostra **A guardar no dispositivo…** até confirmar a gravação. Em caso de falha, conserva as alterações em memória, mostra o erro e tenta novamente. Não indica que uma resposta ficou guardada antes dessa confirmação. Alterações ainda em gravação podem perder-se se o processo for terminado à força; ao sair normalmente, mudar de conta/servidor ou passar para segundo plano, a app espera pelas gravações. Esta espera pode notar-se nessas transições, mas deixa de ocorrer a cada toque de resposta.

As gravações completas do cache e as operações que dependem dos diários aguardam a fila para impedir que uma escrita antiga sobreponha dados mais recentes. A sincronização e as fotografias continuam a usar o comportamento anterior.

A interface reutiliza os dados atuais ao calcular contadores/validações e deixa de notificar os modelos de grupos e contratos quando apenas muda uma resposta. A conclusão da gravação tem uma notificação própria e não atualiza desnecessariamente toda a página.

## Verificação

- Teste C++ com escrita artificial de 500 ms: as duas chamadas de resposta mediram menos de 1 ms neste ambiente; o ciclo de eventos continuou ativo durante as escritas. Atraso de teste disponível apenas em builds com LEM_TESTING.
- Alteração durante uma escrita, recuperação da última resposta após reinício, saída imediata com gravação pendente e falha de escrita seguida de recuperação automática.
- Testes HTTP locais descartáveis de sincronização, conflitos, fotografias, remoção, cache e navegação; testes QML em desktop e dimensões Android.
- Comparação isolada de 40 alterações com backend QML simulado: média de 35,96 ms para 18,88 ms por alteração; leituras da propriedade current de 65 960 para 5 920. Estes valores são medições deste ambiente Linux, não do telefone Android.

É necessário recompilar o cliente. O CMake passa a ligar também Qt6::Concurrent, incluído no Qt. Servidor e base de dados mantêm-se. Verificar o resultado final no equipamento Android; os testes C++ locais usam Qt 6.10.2, enquanto o kit do projeto permanece Qt 6.12.
